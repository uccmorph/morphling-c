#include <unistd.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <functional>

std::mutex mtx;
int num = 0;
std::vector<int> reader_result;
std::atomic_flag flag;
std::atomic<int> int_flag;

const int adder_loop = 10;
using clocking = std::chrono::steady_clock;

void adder() {
  for (int i = 0; i < adder_loop; i++) {
    mtx.lock();
    num += 1;
    mtx.unlock();
  }
}

void adder_atomic_flag() {
  int a = 1;
  for (int i = 0; i < adder_loop; i++) {
    while (flag.test_and_set(std::memory_order_relaxed)) {}
    num += 1;
    flag.clear(std::memory_order_relaxed);
  }
}

struct MockData {
  uint64_t in_change = 0;
  int data[4];
  MockData() {
    for (int i = 0; i < sizeof(data)/sizeof(int); i++) {
      data[i] = 0;
    }
  }
};

MockData g_data;

/*
Test on WSL.
If we commet the lock, then:
  inconsistency: 987272 - 987271
  inconsistency: 992018 - 992017
  inconsistency: 996458 - 996457
  inconsistency: 528074 - 528073
  inconsistency: 964032 - 964031
  num = 0, in 15749800 us
  g_data = 1000000 1000000 1000000 1000000

If we add the atomic lock with memory_order_relaxed, then:
  ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
  num = 0, in 806148900 us
  g_data = 1000000 1000000 1000000 1000000

It's much slower than no-lock version.
It's not a desired behaviour since whatever memory order we use, the taken time is similar.
 */
void adder_atomic_copy(int id) {
  for (int i = 0; i < adder_loop; i++) {
    while (flag.test_and_set(std::memory_order_relaxed)) {}
    if (id == 0) {
      for (int i = 0; i < sizeof(g_data.data)/sizeof(int); i++) {
        g_data.data[i] += 1;
      }
    } else {
      MockData md;
      memcpy(&md, &g_data, sizeof(MockData));
      bool ckech_same = true;
      for (int i = 0; i < sizeof(g_data.data)/sizeof(int)-1; i++) {
        if (md.data[i] != md.data[i+1]) {
          printf("inconsistency: %d - %d\n", md.data[i], md.data[i+1]);
        }
      }
    }
    flag.clear(std::memory_order_relaxed);
  }
}

/*
Test on WSL.
fence with memory_order_relaxed:
  inconsistency: 998530 - 998529
  inconsistency: 998845 - 998844
  inconsistency: 999689 - 999688
  inconsistency: 995370 - 995369
  inconsistency: 998885 - 998884
  num = 0, in 70274800 us
  g_data = 1000000 1000000 1000000 1000000


*/
void adder_barrier_copy(int id) {
  for (int i = 0; i < adder_loop; i++) {
    if (id == 0) {
      g_data.in_change = 1;
      std::atomic_thread_fence(std::memory_order_release);
      for (int i = 0; i < sizeof(g_data.data)/sizeof(int); i++) {
        g_data.data[i] += 1;
      }
      std::atomic_thread_fence(std::memory_order_release);
      g_data.in_change = 0;
      std::atomic_thread_fence(std::memory_order_release);
    } else {
      std::atomic_thread_fence(std::memory_order_acquire);
      MockData md;
      memcpy(&md, &g_data, sizeof(MockData));
      bool ckech_same = true;
      for (int i = 0; i < sizeof(g_data.data)/sizeof(int)-1; i++) {
        if (md.in_change == 0 && md.data[i] != md.data[i+1]) {
          printf("integrity fail: %d - %d\n", md.data[i], md.data[i+1]);
        }
      }
    }
  }
}

static const uint64_t crc64_tab[256] = {
	0x0000000000000000ULL, 0x42F0E1EBA9EA3693ULL, 0x85E1C3D753D46D26ULL,
	0xC711223CFA3E5BB5ULL, 0x493366450E42ECDFULL, 0x0BC387AEA7A8DA4CULL,
	0xCCD2A5925D9681F9ULL, 0x8E224479F47CB76AULL, 0x9266CC8A1C85D9BEULL,
	0xD0962D61B56FEF2DULL, 0x17870F5D4F51B498ULL, 0x5577EEB6E6BB820BULL,
	0xDB55AACF12C73561ULL, 0x99A54B24BB2D03F2ULL, 0x5EB4691841135847ULL,
	0x1C4488F3E8F96ED4ULL, 0x663D78FF90E185EFULL, 0x24CD9914390BB37CULL,
	0xE3DCBB28C335E8C9ULL, 0xA12C5AC36ADFDE5AULL, 0x2F0E1EBA9EA36930ULL,
	0x6DFEFF5137495FA3ULL, 0xAAEFDD6DCD770416ULL, 0xE81F3C86649D3285ULL,
	0xF45BB4758C645C51ULL, 0xB6AB559E258E6AC2ULL, 0x71BA77A2DFB03177ULL,
	0x334A9649765A07E4ULL, 0xBD68D2308226B08EULL, 0xFF9833DB2BCC861DULL,
	0x388911E7D1F2DDA8ULL, 0x7A79F00C7818EB3BULL, 0xCC7AF1FF21C30BDEULL,
	0x8E8A101488293D4DULL, 0x499B3228721766F8ULL, 0x0B6BD3C3DBFD506BULL,
	0x854997BA2F81E701ULL, 0xC7B97651866BD192ULL, 0x00A8546D7C558A27ULL,
	0x4258B586D5BFBCB4ULL, 0x5E1C3D753D46D260ULL, 0x1CECDC9E94ACE4F3ULL,
	0xDBFDFEA26E92BF46ULL, 0x990D1F49C77889D5ULL, 0x172F5B3033043EBFULL,
	0x55DFBADB9AEE082CULL, 0x92CE98E760D05399ULL, 0xD03E790CC93A650AULL,
	0xAA478900B1228E31ULL, 0xE8B768EB18C8B8A2ULL, 0x2FA64AD7E2F6E317ULL,
	0x6D56AB3C4B1CD584ULL, 0xE374EF45BF6062EEULL, 0xA1840EAE168A547DULL,
	0x66952C92ECB40FC8ULL, 0x2465CD79455E395BULL, 0x3821458AADA7578FULL,
	0x7AD1A461044D611CULL, 0xBDC0865DFE733AA9ULL, 0xFF3067B657990C3AULL,
	0x711223CFA3E5BB50ULL, 0x33E2C2240A0F8DC3ULL, 0xF4F3E018F031D676ULL,
	0xB60301F359DBE0E5ULL, 0xDA050215EA6C212FULL, 0x98F5E3FE438617BCULL,
	0x5FE4C1C2B9B84C09ULL, 0x1D14202910527A9AULL, 0x93366450E42ECDF0ULL,
	0xD1C685BB4DC4FB63ULL, 0x16D7A787B7FAA0D6ULL, 0x5427466C1E109645ULL,
	0x4863CE9FF6E9F891ULL, 0x0A932F745F03CE02ULL, 0xCD820D48A53D95B7ULL,
	0x8F72ECA30CD7A324ULL, 0x0150A8DAF8AB144EULL, 0x43A04931514122DDULL,
	0x84B16B0DAB7F7968ULL, 0xC6418AE602954FFBULL, 0xBC387AEA7A8DA4C0ULL,
	0xFEC89B01D3679253ULL, 0x39D9B93D2959C9E6ULL, 0x7B2958D680B3FF75ULL,
	0xF50B1CAF74CF481FULL, 0xB7FBFD44DD257E8CULL, 0x70EADF78271B2539ULL,
	0x321A3E938EF113AAULL, 0x2E5EB66066087D7EULL, 0x6CAE578BCFE24BEDULL,
	0xABBF75B735DC1058ULL, 0xE94F945C9C3626CBULL, 0x676DD025684A91A1ULL,
	0x259D31CEC1A0A732ULL, 0xE28C13F23B9EFC87ULL, 0xA07CF2199274CA14ULL,
	0x167FF3EACBAF2AF1ULL, 0x548F120162451C62ULL, 0x939E303D987B47D7ULL,
	0xD16ED1D631917144ULL, 0x5F4C95AFC5EDC62EULL, 0x1DBC74446C07F0BDULL,
	0xDAAD56789639AB08ULL, 0x985DB7933FD39D9BULL, 0x84193F60D72AF34FULL,
	0xC6E9DE8B7EC0C5DCULL, 0x01F8FCB784FE9E69ULL, 0x43081D5C2D14A8FAULL,
	0xCD2A5925D9681F90ULL, 0x8FDAB8CE70822903ULL, 0x48CB9AF28ABC72B6ULL,
	0x0A3B7B1923564425ULL, 0x70428B155B4EAF1EULL, 0x32B26AFEF2A4998DULL,
	0xF5A348C2089AC238ULL, 0xB753A929A170F4ABULL, 0x3971ED50550C43C1ULL,
	0x7B810CBBFCE67552ULL, 0xBC902E8706D82EE7ULL, 0xFE60CF6CAF321874ULL,
	0xE224479F47CB76A0ULL, 0xA0D4A674EE214033ULL, 0x67C58448141F1B86ULL,
	0x253565A3BDF52D15ULL, 0xAB1721DA49899A7FULL, 0xE9E7C031E063ACECULL,
	0x2EF6E20D1A5DF759ULL, 0x6C0603E6B3B7C1CAULL, 0xF6FAE5C07D3274CDULL,
	0xB40A042BD4D8425EULL, 0x731B26172EE619EBULL, 0x31EBC7FC870C2F78ULL,
	0xBFC9838573709812ULL, 0xFD39626EDA9AAE81ULL, 0x3A28405220A4F534ULL,
	0x78D8A1B9894EC3A7ULL, 0x649C294A61B7AD73ULL, 0x266CC8A1C85D9BE0ULL,
	0xE17DEA9D3263C055ULL, 0xA38D0B769B89F6C6ULL, 0x2DAF4F0F6FF541ACULL,
	0x6F5FAEE4C61F773FULL, 0xA84E8CD83C212C8AULL, 0xEABE6D3395CB1A19ULL,
	0x90C79D3FEDD3F122ULL, 0xD2377CD44439C7B1ULL, 0x15265EE8BE079C04ULL,
	0x57D6BF0317EDAA97ULL, 0xD9F4FB7AE3911DFDULL, 0x9B041A914A7B2B6EULL,
	0x5C1538ADB04570DBULL, 0x1EE5D94619AF4648ULL, 0x02A151B5F156289CULL,
	0x4051B05E58BC1E0FULL, 0x87409262A28245BAULL, 0xC5B073890B687329ULL,
	0x4B9237F0FF14C443ULL, 0x0962D61B56FEF2D0ULL, 0xCE73F427ACC0A965ULL,
	0x8C8315CC052A9FF6ULL, 0x3A80143F5CF17F13ULL, 0x7870F5D4F51B4980ULL,
	0xBF61D7E80F251235ULL, 0xFD913603A6CF24A6ULL, 0x73B3727A52B393CCULL,
	0x31439391FB59A55FULL, 0xF652B1AD0167FEEAULL, 0xB4A25046A88DC879ULL,
	0xA8E6D8B54074A6ADULL, 0xEA16395EE99E903EULL, 0x2D071B6213A0CB8BULL,
	0x6FF7FA89BA4AFD18ULL, 0xE1D5BEF04E364A72ULL, 0xA3255F1BE7DC7CE1ULL,
	0x64347D271DE22754ULL, 0x26C49CCCB40811C7ULL, 0x5CBD6CC0CC10FAFCULL,
	0x1E4D8D2B65FACC6FULL, 0xD95CAF179FC497DAULL, 0x9BAC4EFC362EA149ULL,
	0x158E0A85C2521623ULL, 0x577EEB6E6BB820B0ULL, 0x906FC95291867B05ULL,
	0xD29F28B9386C4D96ULL, 0xCEDBA04AD0952342ULL, 0x8C2B41A1797F15D1ULL,
	0x4B3A639D83414E64ULL, 0x09CA82762AAB78F7ULL, 0x87E8C60FDED7CF9DULL,
	0xC51827E4773DF90EULL, 0x020905D88D03A2BBULL, 0x40F9E43324E99428ULL,
	0x2CFFE7D5975E55E2ULL, 0x6E0F063E3EB46371ULL, 0xA91E2402C48A38C4ULL,
	0xEBEEC5E96D600E57ULL, 0x65CC8190991CB93DULL, 0x273C607B30F68FAEULL,
	0xE02D4247CAC8D41BULL, 0xA2DDA3AC6322E288ULL, 0xBE992B5F8BDB8C5CULL,
	0xFC69CAB42231BACFULL, 0x3B78E888D80FE17AULL, 0x7988096371E5D7E9ULL,
	0xF7AA4D1A85996083ULL, 0xB55AACF12C735610ULL, 0x724B8ECDD64D0DA5ULL,
	0x30BB6F267FA73B36ULL, 0x4AC29F2A07BFD00DULL, 0x08327EC1AE55E69EULL,
	0xCF235CFD546BBD2BULL, 0x8DD3BD16FD818BB8ULL, 0x03F1F96F09FD3CD2ULL,
	0x41011884A0170A41ULL, 0x86103AB85A2951F4ULL, 0xC4E0DB53F3C36767ULL,
	0xD8A453A01B3A09B3ULL, 0x9A54B24BB2D03F20ULL, 0x5D45907748EE6495ULL,
	0x1FB5719CE1045206ULL, 0x919735E51578E56CULL, 0xD367D40EBC92D3FFULL,
	0x1476F63246AC884AULL, 0x568617D9EF46BED9ULL, 0xE085162AB69D5E3CULL,
	0xA275F7C11F7768AFULL, 0x6564D5FDE549331AULL, 0x279434164CA30589ULL,
	0xA9B6706FB8DFB2E3ULL, 0xEB46918411358470ULL, 0x2C57B3B8EB0BDFC5ULL,
	0x6EA7525342E1E956ULL, 0x72E3DAA0AA188782ULL, 0x30133B4B03F2B111ULL,
	0xF7021977F9CCEAA4ULL, 0xB5F2F89C5026DC37ULL, 0x3BD0BCE5A45A6B5DULL,
	0x79205D0E0DB05DCEULL, 0xBE317F32F78E067BULL, 0xFCC19ED95E6430E8ULL,
	0x86B86ED5267CDBD3ULL, 0xC4488F3E8F96ED40ULL, 0x0359AD0275A8B6F5ULL,
	0x41A94CE9DC428066ULL, 0xCF8B0890283E370CULL, 0x8D7BE97B81D4019FULL,
	0x4A6ACB477BEA5A2AULL, 0x089A2AACD2006CB9ULL, 0x14DEA25F3AF9026DULL,
	0x562E43B4931334FEULL, 0x913F6188692D6F4BULL, 0xD3CF8063C0C759D8ULL,
	0x5DEDC41A34BBEEB2ULL, 0x1F1D25F19D51D821ULL, 0xD80C07CD676F8394ULL,
	0x9AFCE626CE85B507ULL
};

/*
 * This a generic crc64() function, it takes seed as an argument,
 * and does __not__ xor at the end. Then individual users can do
 * whatever they need.
 */
uint64_t crc64(uint64_t seed, const unsigned char *data, size_t len)
{
	uint64_t crc = seed;

	while (len--) {
		int i = ((int) (crc >> 56) ^ *data++) & 0xFF;
		crc = crc64_tab[i] ^ (crc << 8);
	}

	return crc;
}

/*
ucc@desktop-8sjudi:~/code/morphling-c/extra$ g++ -pthread -std=c++17 thread_fence_add.cpp
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
int flag: 1
loop 0 set checksum: 0xa2bcb82a62d48edd
loop 1 set checksum: 0x78991bf6c432b29
checksum, want: 0xa2bcb82a62d48edd, real: 0x78991bf6c432b29
md data = 2 2 2 2
checksum, want: 0xa2bcb82a62d48edd, real: 0x78991bf6c432b29
md data = 2 2 2 2
checksum, want: 0xa2bcb82a62d48edd, real: 0x78991bf6c432b29
md data = 2 2 2 2
checksum, want: 0x78991bf6c432b29, real: 0xa53529950e97a5f4
md data = 3 3 3 3
checksum, want: 0x78991bf6c432b29, real: 0xa53529950e97a5f4
md data = 3 3 3 3
loop 2 set checksum: 0xa53529950e97a5f4
checksum, want: 0x78991bf6c432b29, real: 0xa53529950e97a5f4
md data = 3 3 3 3
checksum, want: 0xa53529950e97a5f4, real: 0xf13237ed8865652
md data = 4 4 4 4
checksum, want: 0xa53529950e97a5f4, real: 0xf13237ed8865652
md data = 4 4 4 4
checksum, want: 0xa53529950e97a5f4, real: 0xf13237ed8865652
md data = 4 4 4 4
loop 3 set checksum: 0xf13237ed8865652
checksum, want: 0xf13237ed8865652, real: 0xadaf9b54ba52d88f
md data = 5 5 5 5
checksum, want: 0xf13237ed8865652, real: 0xadaf9b54ba52d88f
md data = 5 5 5 5
loop 4 set checksum: 0xadaf9b54ba52d88f
checksum, want: 0xf13237ed8865652, real: 0xadaf9b54ba52d88f
md data = 5 5 5 5
checksum, want: 0xadaf9b54ba52d88f, real: 0x89ab2c1b4c57d7b
md data = 6 6 6 6
checksum, want: 0xadaf9b54ba52d88f, real: 0x89ab2c1b4c57d7b
md data = 6 6 6 6
checksum, want: 0xadaf9b54ba52d88f, real: 0x89ab2c1b4c57d7b
md data = 6 6 6 6
loop 5 set checksum: 0x89ab2c1b4c57d7b
checksum, want: 0x89ab2c1b4c57d7b, real: 0xaa260aebd611f3a6
md data = 7 7 7 7
checksum, want: 0x89ab2c1b4c57d7b, real: 0xaa260aebd611f3a6
md data = 7 7 7 7
loop 6 set checksum: 0xaa260aebd611f3a6
checksum, want: 0x89ab2c1b4c57d7b, real: 0xaa260aebd611f3a6
md data = 7 7 7 7
checksum, want: 0xaa260aebd611f3a6, real: 0x1e2646fdb10caca4
md data = 8 8 8 8
checksum, want: 0xaa260aebd611f3a6, real: 0x1e2646fdb10caca4
md data = 8 8 8 8
loop 7 set checksum: 0x1e2646fdb10caca4
checksum, want: 0xaa260aebd611f3a6, real: 0x1e2646fdb10caca4
md data = 8 8 8 8
checksum, want: 0x1e2646fdb10caca4, real: 0xbc9afed7d3d82279
md data = 9 9 9 9
checksum, want: 0x1e2646fdb10caca4, real: 0xbc9afed7d3d82279
md data = 9 9 9 9
loop 8 set checksum: 0xbc9afed7d3d82279
checksum, want: 0x1e2646fdb10caca4, real: 0xbc9afed7d3d82279
md data = 9 9 9 9
checksum, want: 0xbc9afed7d3d82279, real: 0x19afd742dd4f878d
md data = 10 10 10 10
checksum, want: 0xbc9afed7d3d82279, real: 0x19afd742dd4f878d
md data = 10 10 10 10
loop 9 set checksum: 0x19afd742dd4f878d
checksum, want: 0xbc9afed7d3d82279, real: 0x19afd742dd4f878d
md data = 10 10 10 10
thread 3, no_integrity = 9, integrity = 92575
thread 1, no_integrity = 9, integrity = 92770
thread 0, no_integrity = 0, integrity = 0
thread 2, no_integrity = 9, integrity = 93630
num = 0, in 12751800 us
g_data = 10 10 10 10
in_change = 0x19afd742dd4f878d
*/
void adder_hash_checking(int id) {
  int no_integrity = 0;
  int integrity = 0;
  for (int i = 0; ; i++) {
    if (id == 0) {
      if (i >= adder_loop) {
        break;
      }
      for (int i = 0; i < sizeof(g_data.data)/sizeof(int); i++) {
        g_data.data[i] += 1;
      }
      auto checksum = crc64(0, (const unsigned char *)g_data.data, sizeof(g_data.data));
      g_data.in_change = checksum;
      printf("loop %d set checksum: 0x%lx\n", i, checksum);
      usleep(1000);
    } else {
      if (int_flag.load() == 100) {
        break;
      }
      MockData md = g_data;
      // memcpy(&md, &g_data, sizeof(MockData));
      auto checksum = crc64(0, (const unsigned char *)md.data, sizeof(md.data));
      if (checksum != md.in_change) {
        printf("checksum, want: 0x%lx, real: 0x%lx\n", md.in_change, checksum);
        printf("md data = %d %d %d %d\n", md.data[0], md.data[1], md.data[2], md.data[3]);
        no_integrity += 1;
      } else {
        integrity += 1;
      }
      checksum = crc64(0, (const unsigned char *)md.data, sizeof(md.data));
      // printf("checksum, want: 0x%lx, real: 0x%lx\n", md.in_change, checksum);
    }
  }
  if (id == 0) {
    int_flag.store(100);
  }
  printf("thread %d, no_integrity = %d, integrity = %d\n", id, no_integrity, integrity);
}

void crc64_test() {
  char str[16] = "hello world!";
  auto checksum = crc64(0, (const unsigned char *)str, 16);
  printf("checksum: %lx\n", checksum);

  char str2[16] = "hello world!";
  auto checksum2 = crc64(0, (const unsigned char *)str2, 16);
  printf("checksum2: %lx\n", checksum2);

}

void crc64_test2() {
  g_data.data[0] = 0xff;


  for (uint8_t *tmp = (uint8_t *)&g_data; tmp < (uint8_t *)&g_data + sizeof(MockData); tmp++) {
    printf("%x ", *tmp);
  }
  printf("\n");
  MockData md = g_data;
  for (uint8_t *tmp = (uint8_t *)&md; tmp < (uint8_t *)&md + sizeof(MockData); tmp++) {
    printf("%x ", *tmp);
  }
  printf("\n");
  auto checksum = crc64(0, (const unsigned char *)g_data.data, sizeof(g_data.data));
  auto checksum2 = crc64(0, (const unsigned char *)md.data, sizeof(md.data));
  printf("checksum: %lx, size: %zu\n", checksum, sizeof(g_data.data));
  printf("checksum2: %lx, size: %zu\n", checksum2, sizeof(md.data));
}

int main() {

  printf("int flag: %d\n", int_flag.is_lock_free());
  // crc64_test2();
  // auto res = flag.test_and_set();
  // printf("res = %d\n", res);

  // flag.clear();

  auto thread_num = 4;
  auto start = clocking::now();
  std::vector<std::thread> ts;
  for (int i = 0; i < thread_num; i++) {
    ts.emplace_back(adder_hash_checking, i);
  }

  for (int i = 0; i < thread_num; i++) {
    ts[i].join();
  }
  auto end = clocking::now();
  auto diff = (end-start).count();
  printf("num = %d, in %ld us\n", num, diff);
  printf("g_data = %d %d %d %d\n", g_data.data[0], g_data.data[1], g_data.data[2], g_data.data[3]);
  printf("in_change = 0x%lx\n", g_data.in_change);
}