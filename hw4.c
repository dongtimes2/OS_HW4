//
// Simple FIle System
// Student Name : 유동하
// Student Number : B731165
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* optional */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/***********/

#include "sfs_types.h"
#include "sfs_func.h"
#include "sfs_disk.h"
#include "sfs.h"

void dump_directory();

/* BIT operation Macros */
/* a=target variable, b=bit number to act upon 0-n */
#define BIT_SET(a,b) ((a) |= (1<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1<<(b)))
#define BIT_FLIP(a,b) ((a) ^= (1<<(b)))
#define BIT_CHECK(a,b) ((a) & (1<<(b)))

static struct sfs_super spb;	// superblock
static struct sfs_dir sd_cwd = { SFS_NOINO }; // current working directory

void error_message(const char *message, const char *path, int error_code) {
	switch (error_code) {
	case -1:
		printf("%s: %s: No such file or directory",message, path); return;
	case -2:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -3:
		printf("%s: %s: Directory full\n",message, path); return;
	case -4:
		printf("%s: %s: No block available\n",message, path); return;
	case -5:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -6:
		printf("%s: %s: Already exists\n",message, path); return;
	case -7:
		printf("%s: %s: Directory not empty\n",message, path); return;
	case -8:
		printf("%s: %s: Invalid argument\n",message, path); return;
	case -9:
		printf("%s: %s: Is a directory\n",message, path); return;
	case -10:
		printf("%s: %s: Is not a file\n",message, path); return;
	case -11:
		printf("%s: input file size exceeds the max file size\n",message); return;
	case -12:
		printf("%s: can't open %s input file\n",message, path); return;
	default:
		printf("unknown error code\n");
		return;
	}
}

double f_ceil(double value){
	int num = (int)value;
	if((double)num == value) return value;
	else return value+1;
}

void sfs_cpin(const char* local_path, const char* out_path)
{
	int i,j,k,l,m;
	int dataI, dataJ, dataK;
	int size;
	int calcualtedInodeNum;
	int deletedInodeNum;
	int bitSetFlag=0;
	int bitSetFlag2=0;
	int allocatedFlag=0;
	int allocatedFlag2=0;
	int allocatedFlag3=0;
	int notCompleteFlag=0;
	int over7680Flag=0;
	u_int32_t needBitCnt=0;
	u_int32_t checkBitCnt=0;

	struct sfs_inode currentInode;
	struct sfs_inode newInode;
	struct sfs_dir dir[SFS_DENTRYPERBLOCK];
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];
	u_int32_t indirectArr[SFS_DBPERIDB]={};
	char bitMap[SFS_BLOCKSIZE];

	FILE *fp = fopen(out_path, "rb");

	disk_read(&currentInode, sd_cwd.sfd_ino);

	// 이름 중복 찾기
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){
					// 해당 path가 존재하는 경우 => 중복. 리턴시켜서 함수 끝냄
					if(!strcmp(checkDir[j].sfd_name, local_path)){
						error_message("cpin", local_path, -6);
						return;
					}
				}
			}
		}
	}

	// 만약 i node의 디렉토리가 꽉찼으면 error!
	// 현재 inode의 사이즈가 7680 이상이면 꽉찬 것이다. 512*15 = 7680
	if(currentInode.sfi_size>=7680){
		error_message("cpin", local_path, -3);
		return;
	}

	// 에러 out_path가 없는 경우
	if(fp==NULL){
		error_message("cpin", out_path, -12);
		return;
	}

	// path 파일 사이즈가 허용하는 범위 이상인 경우
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);

	// printf("debug : size : %d\n", size);
	if(size>73216){
		error_message("cpin", out_path, -11);
		return;
	}

	// 할당 매커니즘 => touch와 동일
	// 파일 사이즈가 7680이내일 경우 => 파일 inode 할당 + 해당 사이즈만큼의 direct data block 할당
	// 파일 사이즈가 7680초과일 경우 => 파일 inode 할강 + 해당 사이즈만큼의 direct data block 할당 + indirect node 할당 + indirect data 할당

	if(size>7680){
		over7680Flag=1;
	}

	//cwd directory entry에 공간 있는지 확인함 => allocatedFlag
	for(i=0; i<SFS_NDIRECT; i++){
		if(allocatedFlag) break;
		if(currentInode.sfi_direct[i]){
			disk_read(dir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(!dir[j].sfd_ino){
					allocatedFlag=1;
					break;
				}
			}
		}
	}

	if(allocatedFlag){
		if(!over7680Flag) needBitCnt = f_ceil(((double)size/(double)512)) + 1;
		else needBitCnt = f_ceil(((double)size/(double)512)) + 2;
	}

	else{
		if(!over7680Flag) needBitCnt = f_ceil(((double)size/(double)512)) + 2;
		else needBitCnt = f_ceil(((double)size/(double)512)) + 3;
	}

	//할당된 비트의 인덱스 정보를 저장하는 배열을 생성
	int *bitArr = (int*)malloc(sizeof(int)* needBitCnt);
	memset(bitArr, 0, sizeof(int)* needBitCnt);

	//필요한 비트의 개수 만큼 비트를 찾아 배열에 저장하는 과정
	for(l=0; l<needBitCnt; l++){
		bitSetFlag=0;
		// bitmap에서 0인 비트를 needBitCnt 개수만큼 찾아야 한다.
		for(i=0; i<SFS_BITBLOCKS(spb.sp_nblocks); i++){
			if(bitSetFlag) break;

			disk_read(bitMap, i+SFS_MAP_LOCATION);

			for(j=0; j<SFS_BLOCKSIZE; j++){
				if(bitSetFlag) break;

				for(k=0; k<8; k++){
					// 0인 비트를 찾았을 때 이를 1로 세팅한다.
					// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
					if(!BIT_CHECK(bitMap[j], k)){
						// printf("debug : BIT_CHECK 성공\n");
						BIT_SET(bitMap[j], k);
						bitSetFlag=1;
						calcualtedInodeNum=(8*SFS_BLOCKSIZE*i+8*j+k);
						bitArr[l]=calcualtedInodeNum;
						checkBitCnt++;
						//printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum);
						disk_write(bitMap, i+SFS_MAP_LOCATION);
						break;
					}
				}
			}
		}
	}

	//단 한개의 공간도 없을 경우 에러
	if(!checkBitCnt){
		error_message("cpin", local_path, -4);
		return;
	}

	//남은 공간은 1개인데 cwd 할당이 필요한 경우 에러. 이때는 BIT_CLEAR 해줘야 함
	if(checkBitCnt==1 && !allocatedFlag){
		for(l=0; l<needBitCnt; l++){
			if(!bitArr[l]){
				deletedInodeNum = bitArr[l];

				dataI = deletedInodeNum / (512*8);
				deletedInodeNum%=(512*8);
				dataJ = deletedInodeNum / 8;
				deletedInodeNum%=8;
				dataK = deletedInodeNum;

				disk_read(bitMap, dataI+SFS_MAP_LOCATION);
				BIT_CLEAR(bitMap[dataJ], dataK);
				disk_write(bitMap, dataI+SFS_MAP_LOCATION);
			}
		}
		error_message("cpin", local_path, -4);
		return;
	}

	//확인한 비트의 개수와 필요한 비트의 개수가 일치하는지 확인
	//만약 필요한 비트의 개수보다 잔여 비트개수가 적을 경우 notCompleteFlag
	if(checkBitCnt!=needBitCnt){
		notCompleteFlag=1;
	}

	//만약 공급 >= 수요 일 경우
	if(!notCompleteFlag){

		//cwd 추가가 필요없을 경우 그냥 그대로...
		if(allocatedFlag){

			for(i=0; i<SFS_NDIRECT; i++){
				if(allocatedFlag3) break;
				if(currentInode.sfi_direct[i]){
					disk_read(dir, currentInode.sfi_direct[i]);

					for(j=0; j<SFS_DENTRYPERBLOCK; j++){
						if(!dir[j].sfd_ino){
							dir[j].sfd_ino = bitArr[0];
							strncpy(dir[j].sfd_name, local_path, SFS_NAMELEN);
							disk_write(dir, currentInode.sfi_direct[i]);
							allocatedFlag3=1;
							break;
						}
					}
				}
			}

			bzero(&newInode, SFS_BLOCKSIZE);
			newInode.sfi_size = size;
			newInode.sfi_type = SFS_TYPE_FILE;
			disk_write(&newInode, bitArr[0]);

			// 사이즈가 7680 이내일 경우
			if(!over7680Flag){
				for(l=1; l<needBitCnt; l++){
					newInode.sfi_direct[l-1]=bitArr[l];
					disk_write(&newInode, bitArr[0]);
				}
			}

			// 사이즈가 7680 초과할 경우
			else{
				for(l=1; l<16; l++){
					newInode.sfi_direct[l-1]=bitArr[l];
					disk_write(&newInode, bitArr[0]);
				}

				for(l=16; l<needBitCnt; l++){
					if(l==16) {
						newInode.sfi_indirect=bitArr[l];
						disk_write(&newInode, bitArr[0]);
					}

					else{
						indirectArr[l-17]=bitArr[l];
						disk_write(indirectArr, newInode.sfi_indirect);
					}
				}
			}
		}

		//cwd 추가가 필요한 경우 배열의 맨 첫 번째는 cwd에게...
		else{
			//cwd direct entry 새롭게 할당하는 부분
			for(i=0; i<SFS_NDIRECT; i++){
				if(allocatedFlag2) break;

				if(!currentInode.sfi_direct[i]){
					currentInode.sfi_direct[i] = bitArr[0];

					disk_read(dir, currentInode.sfi_direct[i]);
					for(m=0; m<SFS_DENTRYPERBLOCK; m++){
						dir[m].sfd_ino=SFS_NOINO;
						strcpy(dir[m].sfd_name, "");
					}

					for(j=0; j<SFS_DENTRYPERBLOCK; j++){
						if(!dir[j].sfd_ino){
							dir[j].sfd_ino = bitArr[1];
							strncpy(dir[j].sfd_name, local_path, SFS_NAMELEN);
							disk_write(dir, currentInode.sfi_direct[i]);
							allocatedFlag2=1;
							break;
						}
					}
				}
			}

			bzero(&newInode, SFS_BLOCKSIZE);
			newInode.sfi_size = size;
			newInode.sfi_type = SFS_TYPE_FILE;
			disk_write(&newInode, bitArr[1]);

			// 사이즈가 7680 이내일 경우
			if(!over7680Flag){
				for(l=2; l<needBitCnt; l++){
					newInode.sfi_direct[l-2]=bitArr[l];
					disk_write(&newInode, bitArr[1]);
				}
			}
			// 사이즈가 7680 초과할 경우
			else{
				for(l=2; l<17; l++){
					newInode.sfi_direct[l-2]=bitArr[l];
					disk_write(&newInode, bitArr[1]);
				}

				for(l=17; l<needBitCnt; l++){
					if(l==17){
						newInode.sfi_indirect=bitArr[l];
						disk_write(&newInode, bitArr[1]);
					}

					else{
						indirectArr[l-18]=bitArr[l];
						disk_write(indirectArr, newInode.sfi_indirect);
					}
				}
			}
		}

		currentInode.sfi_size += sizeof(struct sfs_dir);
		disk_write(&currentInode, sd_cwd.sfd_ino);
		free(bitArr);
	}

	//만약 공급 < 수요 일 경우
	else{
		//cwd 추가가 필요없는 경우
		if(allocatedFlag){
			for(i=0; i<SFS_NDIRECT; i++){
				if(allocatedFlag3) break;
				if(currentInode.sfi_direct[i]){
					disk_read(dir, currentInode.sfi_direct[i]);

					for(j=0; j<SFS_DENTRYPERBLOCK; j++){
						if(!dir[j].sfd_ino){
							dir[j].sfd_ino = bitArr[0];
							strncpy(dir[j].sfd_name, local_path, SFS_NAMELEN);
							disk_write(dir, currentInode.sfi_direct[i]);
							allocatedFlag3=1;
							break;
						}
					}
				}
			}

			bzero(&newInode, SFS_BLOCKSIZE);

			if(checkBitCnt<=16){
				newInode.sfi_size = (checkBitCnt-1)*512;
			}
			else{
				if(!over7680Flag){
					newInode.sfi_size = (checkBitCnt-1)*512;
				}
				else{
					newInode.sfi_size = (checkBitCnt-2)*512;
				}
			}

			newInode.sfi_type = SFS_TYPE_FILE;
			disk_write(&newInode, bitArr[0]);

			// 사이즈가 7680 이내일 경우
			if(!over7680Flag){
				for(l=1; l<checkBitCnt; l++){
					//printf("debug : 비트 모자라고 사이즈가 7680 이내\n");
					newInode.sfi_direct[l-1]=bitArr[l];
					disk_write(&newInode, bitArr[0]);
				}
			}

			// 사이즈가 7680 초과할 경우

			//checkBitCnt이 1일 경우 inode만 채울 수 있음
			//checkBitCnt이 2~16인 경우 direct만 채울 수 있음
			//checkBitCnt이 17인 경우 indirect index만 채울 수 있음
			//checkBitCnt이 18~인 경우 inderect의 data만 채울 수 있음

			else{
				if(checkBitCnt<=16){
					for(l=1; l<checkBitCnt; l++){
						newInode.sfi_direct[l-1]=bitArr[l];
						disk_write(&newInode, bitArr[0]);
					}
				}

				else if(checkBitCnt==17){
					for(l=1; l<16; l++){
						newInode.sfi_direct[l-1]=bitArr[l];
						disk_write(&newInode, bitArr[0]);
					}

					newInode.sfi_indirect=bitArr[l];
					disk_write(&newInode, bitArr[0]);
				}

				else{
					for(l=1; l<16; l++){
						newInode.sfi_direct[l-1]=bitArr[l];
						disk_write(&newInode, bitArr[0]);
					}

					for(l=16; l<checkBitCnt; l++){
						if(l==16){
							newInode.sfi_indirect=bitArr[l];
							disk_write(&newInode, bitArr[0]);
						}

						else{
							indirectArr[l-17]=bitArr[l];
							disk_write(indirectArr, newInode.sfi_indirect);
						}
					}
				}
			}
		}

		//cwd 추가가 필요한 경우 배열의 첫 번째는 cwd에게 할당해야 함
		else{
			//cwd direct entry 새롭게 할당하는 부분
			for(i=0; i<SFS_NDIRECT; i++){
				if(allocatedFlag2) break;

				if(!currentInode.sfi_direct[i]){
					currentInode.sfi_direct[i] = bitArr[0];

					disk_read(dir, currentInode.sfi_direct[i]);
					for(m=0; m<SFS_DENTRYPERBLOCK; m++){
						dir[m].sfd_ino=SFS_NOINO;
						strcpy(dir[m].sfd_name, "");
					}

					for(j=0; j<SFS_DENTRYPERBLOCK; j++){
						if(!dir[j].sfd_ino){
							dir[j].sfd_ino = bitArr[1];
							strncpy(dir[j].sfd_name, local_path, SFS_NAMELEN);
							disk_write(dir, currentInode.sfi_direct[i]);
							allocatedFlag2=1;
							break;
						}
					}
				}
			}

			bzero(&newInode, SFS_BLOCKSIZE);

			if(checkBitCnt<=17){
				newInode.sfi_size = (checkBitCnt-2)*512;
			}
			else{
				if(!over7680Flag){
					newInode.sfi_size = (checkBitCnt-2)*512;
				}
				else{
					newInode.sfi_size = (checkBitCnt-3)*512;
				}
			}

			newInode.sfi_type = SFS_TYPE_FILE;
			disk_write(&newInode, bitArr[1]);

			// 사이즈가 7680 이내일 경우
			if(!over7680Flag){
				for(l=2; l<checkBitCnt; l++){
					newInode.sfi_direct[l-2]=bitArr[l];
					disk_write(&newInode, bitArr[1]);
				}
			}

			// 사이즈가 7680 초과할 경우

			//checkBitCnt이 2일 경우 inode만 채울 수 있음
			//checkBitCnt이 3~17인 경우 direct만 채울 수 있음
			//checkBitCnt이 18인 경우 indirect index만 채울 수 있음
			//checkBitCnt이 19~인 경우 inderect의 data만 채울 수 있음

			else{
				if(checkBitCnt<=17){
					for(l=2; l<checkBitCnt; l++){
						newInode.sfi_direct[l-2]=bitArr[l];
						disk_write(&newInode, bitArr[1]);
					}
				}

				else if(checkBitCnt==18){
					for(l=2; l<17; l++){
						newInode.sfi_direct[l-2]=bitArr[l];
						disk_write(&newInode, bitArr[1]);
					}

					newInode.sfi_indirect=bitArr[l];
					disk_write(&newInode, bitArr[1]);
				}

				else{
					for(l=2; l<17; l++){
						newInode.sfi_direct[l-2]=bitArr[l];
						disk_write(&newInode, bitArr[1]);
					}

					for(l=17; l<checkBitCnt; l++){
						if(l==17){
							newInode.sfi_indirect=bitArr[l];
							disk_write(&newInode, bitArr[1]);
						}

						else{
							indirectArr[l-18]=bitArr[l];
							disk_write(indirectArr, newInode.sfi_indirect);
						}
					}
				}
			}
		}

		currentInode.sfi_size += sizeof(struct sfs_dir);
		disk_write(&currentInode, sd_cwd.sfd_ino);
		free(bitArr);

		error_message("cpin", local_path, -4);
	}

	fclose(fp);
}

//file 만들기
void sfs_touch(const char* path)
{
	int i, j, k, l, m;
	int memoryI, memoryJ, memoryK;
	u_int32_t calcualtedInodeNum;
	u_int32_t calcualtedInodeNum2;
	int bitSetFlag=0;
	int bitSetFlag2=0;
	int allocatedFlag=0;
	int allocatedFlag2=0;

	struct sfs_inode currentInode;
	struct sfs_inode newInode;
	struct sfs_dir dir[SFS_DENTRYPERBLOCK];
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];
	char bitMap[SFS_BLOCKSIZE];

	// 현재 cwd의 아이노드 번호를 가진 아이노드 구조체 소환
	disk_read(&currentInode, sd_cwd.sfd_ino);
	assert(currentInode.sfi_type == SFS_TYPE_DIR);

	// 이름 중복 찾기
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){
					// 해당 path가 존재하는 경우 => 중복. 리턴시켜서 함수 끝냄
					if(!strcmp(checkDir[j].sfd_name, path)){
						error_message("touch", path, -6);
						return;
					}
				}
			}
		}
	}

	// 만약 i node의 디렉토리가 꽉찼으면 error!
	// 현재 inode의 사이즈가 7680 이상이면 꽉찬 것이다. 512*15 = 7680
	if(currentInode.sfi_size>=7680){
		error_message("touch", path, -3);
		return;
	}

	// bitmap에서 0인 비트 찾기
	for(i=0; i<SFS_BITBLOCKS(spb.sp_nblocks); i++){
		if(bitSetFlag){
			// printf("debug : 비트플래그 달성, i 탈출\n");
			break;
		}

		disk_read(bitMap, i+SFS_MAP_LOCATION);

		for(j=0; j<SFS_BLOCKSIZE; j++){
			if(bitSetFlag){
				// printf("debug : 비트플래그 달성, j 탈출\n");
				break;
			}

			for(k=0; k<8; k++){
				// 0인 비트를 찾았을 때 이를 1로 세팅한다.
				// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
				if(!BIT_CHECK(bitMap[j], k)){
					// printf("debug : BIT_CHECK 성공\n");
					BIT_SET(bitMap[j], k);
					bitSetFlag=1;
					calcualtedInodeNum=(8*SFS_BLOCKSIZE*i+8*j+k);
					memoryI = i;
					memoryJ = j;
					memoryK = k;
					// printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum);
					disk_write(bitMap, i+SFS_MAP_LOCATION);
					break;
				}
			}
		}
	}

	// 아이노드가 꽉차서 0인 비트를 찾지 못했다면 error!
	if(!bitSetFlag){
		error_message("touch", path, -4);
		return;
	}

	for(i=0; i<SFS_NDIRECT; i++){
		if(allocatedFlag) break;
		if(currentInode.sfi_direct[i]){
			disk_read(dir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				//비어있는 곳에 할당해야 한다.
				if(!dir[j].sfd_ino){
					dir[j].sfd_ino = calcualtedInodeNum;
					strncpy(dir[j].sfd_name, path, SFS_NAMELEN);
					//strcpy(dir[j].sfd_name, path);
					disk_write(dir, currentInode.sfi_direct[i]);
					allocatedFlag=1;
					break;
				}
			}
		}
	}

	if(!allocatedFlag){
		//printf("debug : 데이터가 꽉차서 할당을 못했음!\n");
		//이때는 할당받은 비트를 현재 inode의 데이터 전용으로 사용한 뒤
		//새롭게 비트를 할당하여 그것을 파일의 inode로 활용한다.

		// 파일의 비트로 활용하기 위해 비트를 새롭게 할당하는 부분
		// bitmap에서 0인 비트 찾기
		for(i=0; i<SFS_BITBLOCKS(spb.sp_nblocks); i++){
			if(bitSetFlag2){
				// printf("debug : 비트플래그 달성, i 탈출\n");
				break;
			}

			disk_read(bitMap, i+SFS_MAP_LOCATION);

			for(j=0; j<SFS_BLOCKSIZE; j++){
				if(bitSetFlag2){
					// printf("debug : 비트플래그 달성, j 탈출\n");
					break;
				}

				for(k=0; k<8; k++){
					// 0인 비트를 찾았을 때 이를 1로 세팅한다.
					// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
					if(!BIT_CHECK(bitMap[j], k)){
						// printf("debug : BIT_CHECK 성공\n");
						BIT_SET(bitMap[j], k);
						bitSetFlag2=1;
						calcualtedInodeNum2=(8*SFS_BLOCKSIZE*i+8*j+k);
						// printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum2);
						disk_write(bitMap, i+SFS_MAP_LOCATION);
						break;
					}
				}
			}
		}

		// 아이노드가 꽉차서 0인 비트를 찾지 못했다면 error!
		// 이럴 경우 이전에 할당했던 비트도 반납해야 한다.
		if(!bitSetFlag2){
			disk_read(bitMap, memoryI+SFS_MAP_LOCATION);
			BIT_CLEAR(bitMap[memoryJ], memoryK);
			disk_write(bitMap, memoryI+SFS_MAP_LOCATION);
			error_message("touch", path, -4);
			return;
		}

		for(i=0; i<SFS_NDIRECT; i++){
			// 사용하지 않은 다이렉트를 발견
			if(allocatedFlag2) break;

			if(!currentInode.sfi_direct[i]){
				//printf("debug : 새로 할당할 데이터 위치 번호 : %d\n", i);
				//printf("debug : 새로 할당할 데이터 블록 번호 : %d\n", calcualtedInodeNum);


				currentInode.sfi_direct[i] = calcualtedInodeNum;
				//printf("debug : 결과확인 : %d\n", currentInode.sfi_direct[i]);

				disk_read(dir, currentInode.sfi_direct[i]);
				for(l=0; l<SFS_DENTRYPERBLOCK; l++){
					dir[l].sfd_ino=SFS_NOINO;
					strcpy(dir[l].sfd_name, "");
				}

				//printf("debug : 불러오기 완료\n");

				for(j=0; j<SFS_DENTRYPERBLOCK; j++){
					// 비어있는 곳에 할당해야 한다.
					if(!dir[j].sfd_ino){
						dir[j].sfd_ino = calcualtedInodeNum2;
						strncpy(dir[j].sfd_name, path, SFS_NAMELEN);
						//strcpy(dir[j].sfd_name, path);
						disk_write(dir, currentInode.sfi_direct[i]);
						allocatedFlag2=1;
						break;
					}
				}
			}
		}
	}

	currentInode.sfi_size += sizeof(struct sfs_dir);
	disk_write(&currentInode, sd_cwd.sfd_ino);

	bzero(&newInode, SFS_BLOCKSIZE);
	newInode.sfi_size=0;
	newInode.sfi_type = SFS_TYPE_FILE;
	if(allocatedFlag) disk_write(&newInode, calcualtedInodeNum);
	else disk_write(&newInode, calcualtedInodeNum2);
}

//folder 만들기
void sfs_mkdir(const char* org_path)
{
	int inodeI, inodeJ, inodeK;
	int dataI, dataJ, dataK;
	int memoryI, memoryJ, memoryK;
	int memoryI2, memoryJ2, memoryK2;
	int i,j,k,m,l;
	int calcualtedInodeNum;
	int calcualtedInodeNum2;
	int calcualtedInodeNum3;
	int bitSetFlag;
	int bitSetFlag2;
	int bitSetFlag3;
	int allocatedFlag;
	int allocatedFlag2;

	struct sfs_inode currentInode;
	struct sfs_inode newInode;
	struct sfs_dir dir[SFS_DENTRYPERBLOCK];
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];
	struct sfs_dir folderDir[SFS_DENTRYPERBLOCK];
	char bitMap[SFS_BLOCKSIZE];

	// 현재 cwd의 아이노드 번호를 가진 아이노드 구조체 소환
	disk_read(&currentInode, sd_cwd.sfd_ino);

	// 이름 중복 찾기
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){
					// 해당 path가 존재하는 경우 => 중복. 리턴시켜서 함수 끝냄
					if(!strcmp(checkDir[j].sfd_name, org_path)){
						error_message("mkdir", org_path, -6);
						return;
					}
				}
			}
		}
	}

	// 만약 디렉토리가 꽉찼으면 error!
	// 현재 디렉토리의 사이즈가 7680 이상이면 꽉찬 것이다. 512*15 = 7680
	if(currentInode.sfi_size>=7680){
		error_message("mkdir", org_path, -3);
		return;
	}

	//비트 두 개 할당해야 한다. 하나는 폴더의 inode, 하나는 폴더 데이터 블록
	// bitmap에서 0인 비트 찾기_inode 전용
	for(inodeI=0; inodeI<SFS_BITBLOCKS(spb.sp_nblocks); inodeI++){
		if(bitSetFlag) break;

		disk_read(bitMap, inodeI+SFS_MAP_LOCATION);

		for(inodeJ=0; inodeJ<SFS_BLOCKSIZE; inodeJ++){
			if(bitSetFlag) break;

			for(inodeK=0; inodeK<8; inodeK++){
				// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
				if(!BIT_CHECK(bitMap[inodeJ], inodeK)){
					// printf("debug : BIT_CHECK 성공\n");
					BIT_SET(bitMap[inodeJ], inodeK);
					bitSetFlag=1;
					calcualtedInodeNum=(8*SFS_BLOCKSIZE*inodeI+8*inodeJ+inodeK);
					memoryI = inodeI;
					memoryJ = inodeJ;
					memoryK = inodeK;
					//printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum);
					disk_write(bitMap, inodeI+SFS_MAP_LOCATION);
					break;
				}
			}
		}
	}

	// 0인 비트를 찾지 못했다면 error!
	if(!bitSetFlag){
		error_message("mkdir", org_path, -4);
		return;
	}

	// bitmap에서 0인 비트 찾기_dataBlock 전용
	for(dataI=0; dataI<SFS_BITBLOCKS(spb.sp_nblocks); dataI++){
		if(bitSetFlag2) break;

		disk_read(bitMap, dataI+SFS_MAP_LOCATION);

		for(dataJ=0; dataJ<SFS_BLOCKSIZE; dataJ++){
			if(bitSetFlag2) break;

			for(dataK=0; dataK<8; dataK++){
				// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
				if(!BIT_CHECK(bitMap[dataJ], dataK)){
					// printf("debug : BIT_CHECK 성공\n");
					BIT_SET(bitMap[dataJ], dataK);
					bitSetFlag2=1;
					calcualtedInodeNum2=(8*SFS_BLOCKSIZE*dataI+8*dataJ+dataK);
					memoryI2 = dataI;
					memoryJ2 = dataJ;
					memoryK2 = dataK;
					//printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum2);
					disk_write(bitMap, dataI+SFS_MAP_LOCATION);
					break;
				}
			}
		}
	}

	if(!bitSetFlag2){
		disk_read(bitMap, memoryI+SFS_MAP_LOCATION);
		BIT_CLEAR(bitMap[memoryJ], memoryK);
		disk_write(bitMap, memoryI+SFS_MAP_LOCATION);
		error_message("mkdir", org_path, -4);
		return;
	}

	//printf("debug : iNode 블록 위치 : %d, data 블록 위치 : %d\n", calcualtedInodeNum, calcualtedInodeNum2);

	for(i=0; i<SFS_NDIRECT; i++){
		if(allocatedFlag) break;
		if(currentInode.sfi_direct[i]){
			disk_read(dir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				//비어있는 곳에 할당해야 한다.
				if(!dir[j].sfd_ino){
					dir[j].sfd_ino = calcualtedInodeNum;
					strncpy(dir[j].sfd_name, org_path, SFS_NAMELEN);
					//strcpy(dir[j].sfd_name, org_path);
					disk_write(dir, currentInode.sfi_direct[i]);
					allocatedFlag=1;
					break;
				}
			}
		}
	}

	//현재 cwd의 데이터가 꽉찼을 때!
	if(!allocatedFlag){
		//printf("debug : 데이터가 꽉차서 할당을 못했음!\n");
		//calcualtedInodeNum은 현재 cwd의 데이터 블록으로,
		//calcualtedInodeNum2는 새 폴더의 inode로
		//calcualtedInodeNum3는 새 폴더의 데이터 블록으로 활용한다.

		// 파일의 비트로 활용하기 위해 비트를 새롭게 할당하는 부분
		// bitmap에서 0인 비트 찾기
		for(i=0; i<SFS_BITBLOCKS(spb.sp_nblocks); i++){
			if(bitSetFlag3) break;

			disk_read(bitMap, i+SFS_MAP_LOCATION);

			for(j=0; j<SFS_BLOCKSIZE; j++){
				if(bitSetFlag3) break;

				for(k=0; k<8; k++){
					// 0인 비트를 찾았을 때 이를 1로 세팅한다.
					// printf("debug : 비트체크 %d\n", BIT_CHECK(bitMap[j], k));
					if(!BIT_CHECK(bitMap[j], k)){
						// printf("debug : BIT_CHECK 성공\n");
						BIT_SET(bitMap[j], k);
						bitSetFlag3=1;
						calcualtedInodeNum3=(8*SFS_BLOCKSIZE*i+8*j+k);
						// printf("debug : i: %d, j: %d, k: %d, 성공한 Inode 위치 %d\n", i, j, k, calcualtedInodeNum3);
						disk_write(bitMap, i+SFS_MAP_LOCATION);
						break;
					}
				}
			}
		}

		// cwd의 새로운 데이터 블록을 위한 0인 비트를 찾지 못했다면 error!
		// 총체적 난국! 만들고자 했던 폴더의 inode 비트와 data 비트를 모두 반납해야 한다.
		if(!bitSetFlag3){
			disk_read(bitMap, memoryI+SFS_MAP_LOCATION);
			BIT_CLEAR(bitMap[memoryJ], memoryK);
			disk_write(bitMap, memoryI+SFS_MAP_LOCATION);

			disk_read(bitMap, memoryI2+SFS_MAP_LOCATION);
			BIT_CLEAR(bitMap[memoryJ2], memoryK2);
			disk_write(bitMap, memoryI2+SFS_MAP_LOCATION);

			error_message("mkdir", org_path, -4);
			return;
		}

		for(i=0; i<SFS_NDIRECT; i++){
			// 사용하지 않은 다이렉트를 발견
			if(allocatedFlag2) break;

			if(!currentInode.sfi_direct[i]){
				//printf("debug : 새로 할당할 데이터 위치 번호 : %d\n", i);
				//printf("debug : 새로 할당할 데이터 블록 번호 : %d\n", calcualtedInodeNum);

				currentInode.sfi_direct[i] = calcualtedInodeNum;
				//printf("debug : 결과확인 : %d\n", currentInode.sfi_direct[i]);

				disk_read(dir, currentInode.sfi_direct[i]);
				for(l=0; l<SFS_DENTRYPERBLOCK; l++){
					dir[l].sfd_ino=SFS_NOINO;
					//strcpy(folderDir[l].sfd_name, "");
					strncpy(folderDir[l].sfd_name, "", SFS_NAMELEN);
				}

				//printf("debug : 불러오기 완료\n");

				for(j=0; j<SFS_DENTRYPERBLOCK; j++){
					// 비어있는 곳에 할당해야 한다.
					if(!dir[j].sfd_ino){
						dir[j].sfd_ino = calcualtedInodeNum2;
						strncpy(dir[j].sfd_name, org_path, SFS_NAMELEN);
						//strcpy(dir[j].sfd_name, org_path);
						disk_write(dir, currentInode.sfi_direct[i]);
						allocatedFlag2=1;
						break;
					}
				}
			}
		}
	}

	//cwd가 꽉차지 않은 경우
	if(allocatedFlag){
		bzero(&newInode, SFS_BLOCKSIZE);
		newInode.sfi_size += 2*sizeof(struct sfs_dir);
		newInode.sfi_type = SFS_TYPE_DIR;
		newInode.sfi_direct[0]=calcualtedInodeNum2;

		currentInode.sfi_size += sizeof(struct sfs_dir);
		disk_write(&currentInode, sd_cwd.sfd_ino);

		disk_read(folderDir, calcualtedInodeNum2);
		for(l=0; l<SFS_DENTRYPERBLOCK; l++){
			folderDir[l].sfd_ino=SFS_NOINO;
			strncpy(folderDir[l].sfd_name, "", SFS_NAMELEN);
		}

		folderDir[0].sfd_ino = calcualtedInodeNum;
		strncpy(folderDir[0].sfd_name, ".", SFS_NAMELEN);

		folderDir[1].sfd_ino = sd_cwd.sfd_ino;
		strncpy(folderDir[1].sfd_name, "..", SFS_NAMELEN);

		disk_write(folderDir, calcualtedInodeNum2);
		disk_write(&newInode, calcualtedInodeNum);
	}

	else{

		bzero(&newInode, SFS_BLOCKSIZE);
		newInode.sfi_size += 2*sizeof(struct sfs_dir);
		newInode.sfi_type = SFS_TYPE_DIR;
		newInode.sfi_direct[0]=calcualtedInodeNum3;

		currentInode.sfi_size += sizeof(struct sfs_dir);
		disk_write(&currentInode, sd_cwd.sfd_ino);

		disk_read(folderDir, calcualtedInodeNum3);
		for(l=0; l<SFS_DENTRYPERBLOCK; l++){
			folderDir[l].sfd_ino=SFS_NOINO;
			strncpy(folderDir[l].sfd_name, "", SFS_NAMELEN);
		}

		folderDir[0].sfd_ino = calcualtedInodeNum2;
		strncpy(folderDir[0].sfd_name, ".", SFS_NAMELEN);

		folderDir[1].sfd_ino = sd_cwd.sfd_ino;
		strncpy(folderDir[1].sfd_name, "..", SFS_NAMELEN);

		disk_write(folderDir, calcualtedInodeNum3);
		disk_write(&newInode, calcualtedInodeNum2);
	}
}

//folder 삭제
void sfs_rmdir(const char* org_path)
{
	// path가 없는 경우 -1
	// 해당 경로가 디렉토리가 아닌 경우 -5
	// 디렉토리가 비워져 있지 않은 경우 -7

	int i,j,k,l;
	int dataI, dataJ, dataK;
	int mapI, mapJ, mapK;

	int inodeDataValue;
	int inodeValue;
	int rmdirFlag=0;
	struct sfs_inode currentInode;
	struct sfs_inode checkInode;
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];
	char bitMap[SFS_BLOCKSIZE];

	disk_read(&currentInode, sd_cwd.sfd_ino);

	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){

					//자기 폴더 안에서 자기 자신을 지우려고 하면 에러 -8
					if(!strcmp(".", org_path)){
						error_message("rmdir", org_path, -8);
						return;
					}

					if(!strcmp(checkDir[j].sfd_name, org_path)){
						disk_read(&checkInode, checkDir[j].sfd_ino);

						//rmdir 명령인데 file 지우려고 하면 에러 -5
						if(checkInode.sfi_type==SFS_TYPE_FILE){
							error_message("rmdir", org_path, -5);
							return;
						}

						//디렉토리가 비워져 있는지 확인해야 한다. => 사이즈는 128바이트 이내여야 함
						if(checkInode.sfi_size > 128){
							error_message("rmdir", org_path, -7);
							return;
						}

						//폴더의 데이터 부분 비트도 0으로 세팅해야 함
						for(k=0; k<SFS_NDIRECT; k++){
							if(checkInode.sfi_direct[k]){
								inodeDataValue=checkInode.sfi_direct[k];

								dataI = inodeDataValue / (512*8);
								inodeDataValue %= (512*8);
								dataJ = inodeDataValue / 8;
								inodeDataValue %= 8;
								dataK = inodeDataValue;

								disk_read(bitMap, dataI+SFS_MAP_LOCATION);
								BIT_CLEAR(bitMap[dataJ], dataK);
								disk_write(bitMap, dataI+SFS_MAP_LOCATION);
							}
						}

						inodeValue = checkDir[j].sfd_ino;

						mapI = inodeValue / (512*8);
						inodeValue %= (512*8);
						mapJ = inodeValue / 8;
						inodeValue %= 8;
						mapK = inodeValue;

						disk_read(bitMap, mapI+SFS_MAP_LOCATION);
						BIT_CLEAR(bitMap[mapJ], mapK);
						disk_write(bitMap, mapI+SFS_MAP_LOCATION);

						checkDir[j].sfd_ino=SFS_NOINO;
						disk_write(checkDir, currentInode.sfi_direct[i]);

						currentInode.sfi_size -= sizeof(struct sfs_dir);
						disk_write(&currentInode, sd_cwd.sfd_ino);

						rmdirFlag=1;

					}
				}
			}
		}
	}

	if(!rmdirFlag){
		error_message("rmdir", org_path, -1);
		printf("\n");
	}
}

//file 삭제
void sfs_rm(const char* path)
{
	int i, j, k, l;
	int mapI, mapJ, mapK;
	int dataI, dataJ, dataK;
	int indiI, indiJ, indiK;
	int inodeValue;
	int inodeDataValue;
	int indirectValue;
	int rmFlag=0;
	struct sfs_inode currentInode;
	struct sfs_inode checkInode;
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];
	char bitMap[SFS_BLOCKSIZE];
	u_int32_t indirectArr[SFS_DBPERIDB];

	disk_read(&currentInode, sd_cwd.sfd_ino);

	//file이 아니면 에러 -9
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){

					if(!strcmp(checkDir[j].sfd_name, path)){
						disk_read(&checkInode, checkDir[j].sfd_ino);
						if(checkInode.sfi_type==SFS_TYPE_DIR){
							error_message("rm", path, -9);
							return;
						}

						else{
							//파일이 가지고 있는 데이터도 지워야 한다.
							for(k=0; k<SFS_NDIRECT; k++){
								if(checkInode.sfi_direct[k]){
									inodeDataValue=checkInode.sfi_direct[k];
									checkInode.sfi_direct[k]=0;

									//printf("debug : inodeDataValue : %d\n", inodeDataValue);

									dataI = inodeDataValue / (512*8);
									inodeDataValue %= (512*8);
									dataJ = inodeDataValue / 8;
									inodeDataValue %= 8;
									dataK = inodeDataValue;

									//printf("debug : dataI : %d, dataJ : %d, dataK : %d\n", dataI, dataJ, dataK);

									disk_read(bitMap, dataI+SFS_MAP_LOCATION);
									BIT_CLEAR(bitMap[dataJ], dataK);
									disk_write(bitMap, dataI+SFS_MAP_LOCATION);
								}
							}

							inodeValue = checkDir[j].sfd_ino;

							mapI = inodeValue / (512*8);
							inodeValue %= (512*8);
							mapJ = inodeValue / 8;
							inodeValue %= 8;
							mapK = inodeValue;

							//printf("debug : i : %d, j : %d, k : %d\n", mapI, mapJ, mapK);

							disk_read(bitMap, mapI+SFS_MAP_LOCATION);
							BIT_CLEAR(bitMap[mapJ], mapK);
							disk_write(bitMap, mapI+SFS_MAP_LOCATION);

							//indirect가 가진 값도 지워야 한다.
							if(checkInode.sfi_indirect){
								disk_read(indirectArr, checkInode.sfi_indirect);

								for(l=0; l<SFS_DBPERIDB; l++){
									if(indirectArr[l]){
										indirectValue=indirectArr[l];

										indiI = indirectValue / (512*8);
										indirectValue %= (512*8);
										indiJ = indirectValue/ 8;
										indirectValue %= 8;
										indiK = indirectValue;

										disk_read(bitMap, indiI+SFS_MAP_LOCATION);
										BIT_CLEAR(bitMap[indiJ], indiK);
										disk_write(bitMap, indiI+SFS_MAP_LOCATION);

										indirectArr[l]=0;
									}
								}

								indirectValue = checkInode.sfi_indirect;

								indiI = indirectValue / (512*8);
								indirectValue %= (512*8);
								indiJ = indirectValue/ 8;
								indirectValue %= 8;
								indiK = indirectValue;

								disk_read(bitMap, indiI+SFS_MAP_LOCATION);
								BIT_CLEAR(bitMap[indiJ], indiK);
								disk_write(bitMap, indiI+SFS_MAP_LOCATION);

								disk_write(indirectArr, checkInode.sfi_indirect);
								checkInode.sfi_indirect = SFS_NOINO;
								disk_read(&checkInode, checkDir[j].sfd_ino);
							}

							checkDir[j].sfd_ino=SFS_NOINO;
							disk_write(checkDir, currentInode.sfi_direct[i]);

							currentInode.sfi_size -= sizeof(struct sfs_dir);
							disk_write(&currentInode, sd_cwd.sfd_ino);

							//printf("debug : inode 초기화 한 값 : %d\n", checkDir[j].sfd_ino);
							rmFlag=1;
						}
					}
				}
			}
		}
	}

	if(!rmFlag){
		error_message("rm", path, -1);
		printf("\n");
	}
}

//file 혹은 directory 이름 변경
void sfs_mv(const char* src_name, const char* dst_name)
{
	//에러 path2가 이미 존재하는 경우 에러 -6
	//에러 path1이 존재하지 않는 경우 에러 -1

	int i, j, k;
	int findFlag=0;
	struct sfs_inode currentInode;
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];

	disk_read(&currentInode, sd_cwd.sfd_ino);

	// dst 이름 중복 찾기
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){
					// 해당 path가 존재하는 경우 => 중복. 리턴시켜서 함수 끝냄
					if(!strcmp(checkDir[j].sfd_name, dst_name)){
						error_message("mv", dst_name, -6);
						return;
					}
				}
			}
		}
	}

	// 파일 변경하기
	for(i=0; i<SFS_NDIRECT; i++){
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(checkDir[j].sfd_ino){
					// 해당 path가 존재하는 경우 => 중복. 리턴시켜서 함수 끝냄
					if(!strcmp(checkDir[j].sfd_name, src_name)){
						findFlag = 1;
						strcpy(checkDir[j].sfd_name, dst_name);
						disk_write(checkDir, currentInode.sfi_direct[i]);
						return;
					}
				}
			}
		}
	}

	if(!findFlag){
		error_message("mv", src_name, -1);
		printf("\n");
	}
}

void sfs_cd(const char* path)
{
	// 불러올 path의 i-node 값 가져오기
	// 그 값을 기반으로 sd_cwd.sfd_ino = ? 값 변경하기

	// 불러올 path의 name 값 가져오기
	// 그 값을 기반으로 sd_cwd.sfd_name = ? 값 변경하기

	int i, j, k;
	int findFlag=0;

	struct sfs_inode currentInode;
	struct sfs_inode checkInode;
	struct sfs_dir dir[SFS_DENTRYPERBLOCK];

    // printf("debug : %d\n", SFS_DENTRYPERBLOCK);
	// printf("debub_SFS_BITMAPSIZE : %d\n", SFS_BITMAPSIZE(spb.sp_nblocks));
	// printf("debug_SFS_BITBLOCKS : %d\n", SFS_BITBLOCKS(spb.sp_nblocks));
	// 인자가 없는 경우 root를 cwd로 설정한다.
	if(path==NULL){
		// printf("인자 없!\n");
		sd_cwd.sfd_ino = 1;
		sd_cwd.sfd_name[0] = '/';
		sd_cwd.sfd_name[1] = '\0';
	}

	// 인자가 있는 경우 해당 path를 cwd로 설정한다.
	else{
		// 일단 현재 cwd가 가지고 있는 디렉토리들을 탐색한다.
		// 내가 원하는 path와 가지고 있는 디렉토리를 비교해서 있는지 없는지를 파악한다.
		// printf("인자 있음!\n");
		disk_read(&currentInode, sd_cwd.sfd_ino);

		for(i=0; i<SFS_NDIRECT; i++){
			if(currentInode.sfi_direct[i]){
				disk_read(dir, currentInode.sfi_direct[i]);

				for(j=0; j<SFS_DENTRYPERBLOCK; j++){
					if(dir[j].sfd_ino){
						// 해당 path가 존재하는 경우
						if(!strcmp(dir[j].sfd_name, path)){

							disk_read(&checkInode, dir[j].sfd_ino);

							// 해당 path가 디렉토리인 경우, 해당 디렉토리의 inode 번호로 cwd 설정함, 이름도 변경함
							if(checkInode.sfi_type==2){
								sd_cwd.sfd_ino = dir[j].sfd_ino;

								int k=0;
								while(1){
									if(dir[j].sfd_name[k]=='\0'){
										sd_cwd.sfd_name[k] = '\0';
										break;
									}
									sd_cwd.sfd_name[k] = dir[j].sfd_name[k];
									k++;
								}
							}

							// 해당 path가 파일인 경우
							else{
								error_message("cd", path, -5);
							}
							findFlag=1;
							break;
						}
					}
				}
			}
		}

		// 해당 path가 존재하지 않는 경우
		if(!findFlag){
			error_message("cd", path, -1);
			printf("\n");
		}
	}
}

void sfs_ls(const char* path)
{
	int i, j, k, l;
	int findFlag=0;
	struct sfs_inode currentInode;
	struct sfs_inode checkInode;
	struct sfs_inode checkSubInode;

	struct sfs_dir dir[SFS_DENTRYPERBLOCK]; // 데이터 블록 한 칸에는 SFS_DENTRYPERBLOCK만큼의 디렉토리가 올 수 있다.
	struct sfs_dir subDir[SFS_DENTRYPERBLOCK];

	// 블럭에서 cwd의 inode에 해당하는 값을 currentInode에 넣는다.
	disk_read(&currentInode, sd_cwd.sfd_ino);	//루트를 가리켰을 때 결과 1

	// printf("%d\n", currentInode.sfi_size);	//결과 : 384 (크기는 384이다)
	// printf("%d\n", currentInode.sfi_type); //결과 : 2 (디렉토리이므로)
	// printf("%d\n", currentInode.sfi_direct[0]); //결과 : 5 (5번 블록에는 데이터가 저장되어 있다)

	// SFS_NDIRECT = 한 inode에 들어갈 수 있는 directory의 최대 개수
	// 모든 디렉토리를 찾기 위해 저 개수만큼 반복하여 실행한다.
	// 이때 데이터가 있으면 sfi_direct의 값은 0이 아닌 값이며, 비어있으면 0이다.

	// 인자가 없을 경우 현재 디렉토리의 ls 정보를 출력한다.
	if(path == NULL){
		// printf("인자없음!\n");
		for(i=0; i<SFS_NDIRECT; i++){
			// 비어있지 않다면
			if(currentInode.sfi_direct[i]){
				// 해당 블록에 해당하는 데이터를 dir에 삽입한다.
				disk_read(dir, currentInode.sfi_direct[i]);

				for(j=0; j<SFS_DENTRYPERBLOCK; j++){
					// 비어있지 않으면 출력
					if(dir[j].sfd_ino){

						disk_read(&checkInode, dir[j].sfd_ino);
						// 만약 결과가 디렉토리라면 끝에 /를 붙인다.
						if(checkInode.sfi_type==2){
							printf("%s/", dir[j].sfd_name);
							printf("\t");
						}
						// 그렇지 않고 파일이라면 그냥 출력한다.
						else{
							printf("%s", dir[j].sfd_name);
							printf("\t");
						}
					}
				}
			}
		}
	}

	// 인자가 있을 경우
	// 인자가 디렉토리일 경우 해당 디렉토리의 ls 정보를 출력한다.
	else{
		// printf("인자있음!\n");
		for(i=0; i<SFS_NDIRECT; i++){
			// 비어있지 않다면
			if(currentInode.sfi_direct[i]){
				// 해당 블록에 해당하는 데이터를 dir에 삽입한다.
				disk_read(dir, currentInode.sfi_direct[i]);

				for(j=0; j<SFS_DENTRYPERBLOCK; j++){
					// 비어있지 않으면서 해당 인자와 일치하는걸 찾아야 함
					if(dir[j].sfd_ino){
						// 찾은 경우
						if(!strcmp(dir[j].sfd_name, path)){
							// printf("찾음!\n");
							disk_read(&checkInode, dir[j].sfd_ino);

							// 찾았는데 디렉토리인 경우 해당 이름의 지점으로 접근해야 함
							if(checkInode.sfi_type==2){
								// printf("디렉토리");
								for(k=0; k<SFS_NDIRECT; k++){
									// 비어있지 않을 경우
									if(checkInode.sfi_direct[k]){
										disk_read(subDir, checkInode.sfi_direct[k]);

										for(l=0; l<SFS_DENTRYPERBLOCK; l++){
											// 비어있지 않으면 출력
											if(subDir[l].sfd_ino){

												disk_read(&checkSubInode, subDir[l].sfd_ino);
												// 만약 결과가 디렉토리라면 끝에 /를 붙인다.
												if(checkSubInode.sfi_type==2){
													printf("%s/", subDir[l].sfd_name);
													printf("\t");
												}
												// 그렇지 않고 파일이라면 그냥 출력한다.
												else{
													printf("%s", subDir[l].sfd_name);
													printf("\t");
												}
											}
										}
									}
								}
								findFlag=1;
							}

							// 찾았는데 파일인 경우
							else{
								printf("%s", dir[j].sfd_name);
								findFlag=1;
							}
							break;
						}
					}
				}
			}
		}

		if(!findFlag){
			// 오류처리 : path에 해당하는 디렉토리나 파일이 없으면 에러를 출력한다.
			error_message("ls", path, -1);
		}
	}
	printf("\n");
}

void sfs_cpout(const char* local_path, const char* out_path)
{
	//모든 데이터 블락의 데이터를 out_path로 복사해야 한다.
	//local_path가 없으면 에러 -1

	int i,j,k,l;
	int size;
	int findFlag=0;
	int divideCnt=0;
	int remainCnt=0;
	int indiRemainCnt=0;

	struct sfs_inode currentInode;
	struct sfs_inode newInode;
	struct sfs_dir dir[SFS_DENTRYPERBLOCK];
	struct sfs_dir checkDir[SFS_DENTRYPERBLOCK];

	char buffer[SFS_BLOCKSIZE];
	u_int32_t indirectArr[SFS_DBPERIDB]={};

	disk_read(&currentInode, sd_cwd.sfd_ino);

	//맨 처음 읽어서 있으면 에러.
	FILE *fpRead = fopen(out_path, "rb");
	if(fpRead!=NULL){
		//printf("debug : 외부에 중복된 파일이 있음!\n");
		error_message("cpout", out_path, -6);
		fclose(fpRead);
		return;
	}

	//local_path가 존재하지 않으면 에러
	for(i=0; i<SFS_NDIRECT; i++){
		if(findFlag) break;
		if(currentInode.sfi_direct[i]){
			disk_read(checkDir, currentInode.sfi_direct[i]);

			for(j=0; j<SFS_DENTRYPERBLOCK; j++){
				if(findFlag) break;
				if(checkDir[j].sfd_ino){
					// local_path가 존재하는지 찾는다. 나오지 않으면 에러
					if(!strcmp(checkDir[j].sfd_name, local_path)){
						disk_read(&newInode, checkDir[j].sfd_ino);

						size = newInode.sfi_size;
						//printf("debug : 원본 size : %d\n", size);

						FILE *fpWrite = fopen(out_path, "ab");

						if(size<=7680){

							divideCnt = size / 512;
							remainCnt = size % 512;

							//printf("debug : 몫 : %d\n", divideCnt);
							//printf("debug : 나머지 : %d\n", remainCnt);

							char *buffer2 = (char*)malloc(sizeof(char)* remainCnt);

							for(k=0; k<SFS_NDIRECT; k++){
								if(newInode.sfi_direct[k]){

									if(divideCnt){
										disk_read(buffer, newInode.sfi_direct[k]);
										fwrite(buffer, 512, 1, fpWrite);
										divideCnt--;
										size-=512;
									}

									else{
										//printf("debug : 나머지 사이즈 : %d\n", size);
										disk_read(buffer, newInode.sfi_direct[k]);
										strncpy(buffer2, buffer, size);
										fwrite(buffer2, size, 1, fpWrite);
									}
								}
							}

							free(buffer2);
						}

						else{
							for(k=0; k<SFS_NDIRECT; k++){
								if(newInode.sfi_direct[k]){
									disk_read(buffer, newInode.sfi_direct[k]);
									fwrite(buffer, 512, 1, fpWrite);
								}
							}

							disk_read(indirectArr, newInode.sfi_indirect);

							size -= 7680;
							divideCnt = size/ 512; // 49010 / 512 = 95.7
							remainCnt = size % 512;

							char *buffer2 = (char*)malloc(sizeof(char)* remainCnt);

							for(k=0; k<SFS_DBPERIDB; k++){
								if(indirectArr[k]){

									if(divideCnt){
										disk_read(buffer, indirectArr[k]);
										fwrite(buffer, 512, 1, fpWrite);
										divideCnt--;
										size-=512;
									}

									else{
										disk_read(buffer, indirectArr[k]);
										strncpy(buffer2, buffer, size);
										fwrite(buffer2, size, 1, fpWrite);
									}
								}
							}
							free(buffer2);
						}
						fclose(fpWrite);
						findFlag = 1;
					}
				}
			}
		}
	}

	// local_path가 존재하지 않는 경우 -1
	if(!findFlag){
		error_message("cpout", local_path, -1);
		printf("\n");
		return;
	}
}

void sfs_mount(const char* path)
{
	if( sd_cwd.sfd_ino !=  SFS_NOINO )	// inode 번호가 0번이 아니면 일단 unmount 처리한다.
	{
		//umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));
		sd_cwd.sfd_ino = SFS_NOINO;
	}

	printf("Disk image: %s\n", path);

	disk_open(path);
	// 슈퍼블록은 0번에 위치하므로 0번에 위치한 block의 정보를 spb라는 구조체에 삽입
	disk_read( &spb, SFS_SB_LOCATION );

	printf("Superblock magic: %x\n", spb.sp_magic);

	assert( spb.sp_magic == SFS_MAGIC );

	printf("Number of blocks: %d\n", spb.sp_nblocks);
	printf("Volume name: %s\n", spb.sp_volname);
	printf("%s, mounted\n", spb.sp_volname);

	// 최초 마운트를 수행하면 최초의 cwd는 루트가 되기 때문에 cwd 정보를 나타내는 구조체에
	// 루트의 정보를 삽입하는 과정임
	sd_cwd.sfd_ino = 1;		//init at root
	sd_cwd.sfd_name[0] = '/';
	sd_cwd.sfd_name[1] = '\0';
}

void sfs_umount() {

	// 현재 cwd가 0이 아니라면 == 마운트 된 상태라면 실행함
	if( sd_cwd.sfd_ino !=  SFS_NOINO )
	{
		// umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));

		// 현재 cwd의 inode는 0이다. == 초기화시킴
		sd_cwd.sfd_ino = SFS_NOINO;
	}
}

void dump_inode(struct sfs_inode inode) {
	int i;
	struct sfs_dir dir_entry[SFS_DENTRYPERBLOCK];

	printf("size %d type %d direct ", inode.sfi_size, inode.sfi_type);
	for(i=0; i < SFS_NDIRECT; i++) {
		printf(" %d ", inode.sfi_direct[i]);
	}
	printf(" indirect %d",inode.sfi_indirect);
	printf("\n");

	if (inode.sfi_type == SFS_TYPE_DIR) {
		for(i=0; i < SFS_NDIRECT; i++) {
			if (inode.sfi_direct[i] == 0) break;
			disk_read(dir_entry, inode.sfi_direct[i]);
			dump_directory(dir_entry);
		}
	}

}

void dump_directory(struct sfs_dir dir_entry[]) {
	int i;
	struct sfs_inode inode;
	for(i=0; i < SFS_DENTRYPERBLOCK;i++) {
		printf("%d %s\n",dir_entry[i].sfd_ino, dir_entry[i].sfd_name);
		disk_read(&inode,dir_entry[i].sfd_ino);
		if (inode.sfi_type == SFS_TYPE_FILE) {
			printf("\t");
			dump_inode(inode);
		}
	}
}

void sfs_dump() {
	// dump the current directory structure
	struct sfs_inode c_inode;

	disk_read(&c_inode, sd_cwd.sfd_ino);
	printf("cwd inode %d name %s\n",sd_cwd.sfd_ino,sd_cwd.sfd_name);
	dump_inode(c_inode);
	printf("\n");

}
