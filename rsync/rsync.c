#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define FILE_LEN 20
#define PATH_LEN 200
#define BUF_LEN 512
#define TIME_LEN 50
#define CMD_LEN 200
#define LOG_MAX 200
#define TAR_CMD_LEN 512

#define LOG_FILE "./log.txt"
#define TAR_FILE "../tmp.tar"

typedef enum {false, true}bool;

struct timeval start_time, end_time;	//실행시간 측정
unsigned long long proc_time;			//실행시간 측정

char cwdPath[PATH_LEN];		//작업 디렉터리 절대 경로
char srcPath[PATH_LEN];		//src 디렉터리 절대 경로
char dstPath[PATH_LEN];		//dst 디렉터리 절대 경로

char cmd[CMD_LEN];			//초기에 입력했던 명령어

int logFd;

bool rOption = false;
bool tOption = false;
bool mOption = false;

struct {
	char path[PATH_LEN];
	int size;
	bool isDeleted;
} logList[LOG_MAX];			//log에 기록할 파일 list 저장

int logCnt = 0;				//log에 기록할 파일 개수

int tarSize = 0;			//tOption 시 생성한 tar 파일의 크기

//사용법을 출력하는 함수
void print_usage(void)
{
	printf("Usage\n");
	printf("./ssu_rsync [option] <src> <dst>\n");
	printf("<src>: directory or file\n");
	printf("<dst>: directory\n");
	printf("[option]\n");
	printf("-r: src's subdirectory will be also synced.\n");
	printf("-t: sync with tar\n");
	printf("-m: if dst has some file which not be in src, they will deleted at dst.\n");
	return;
}

//현재 시간 정보 문자열에 저장하는 함수
void make_time_str(char *str)
{
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	strftime(str, TIME_LEN, "%c", tm);
	return;
}

//log 추가하는 함수
void add_log(void)
{
	char buf[BUF_LEN];
	char timeStr[TIME_LEN];
	make_time_str(timeStr);
	sprintf(buf, "[%s] %s\n", timeStr, cmd);	//시간 및 명령어 write
	write(logFd, buf, strlen(buf));
	if(!tOption){								//tOption 아닌 경우
		for(int i = 0; i < logCnt; i++){		//각 파일들에 대한 동기화 정보 write
			memset(buf, (char)0, BUF_LEN);
			if(!logList[i].isDeleted)
				sprintf(buf, "\t%s\t%dbytes\n", logList[i].path, logList[i].size);
			else
				sprintf(buf, "\t%s\tdelete\n", logList[i].path);
			write(logFd, buf, strlen(buf));
		}
	}
	else {				//tOption인 경우
		memset(buf, (char)0, BUF_LEN);
		sprintf(buf, "\ttotalSize %d\n", tarSize);	//tar 파일 size write
		write(logFd, buf, strlen(buf));
		for(int i = 0; i < logCnt; i++){			//각 파일들 상대경로 write
			memset(buf, (char)0, BUF_LEN);
			sprintf(buf, "\t%s\n", logList[i].path);
			write(logFd, buf, strlen(buf));
		}
	}
	return;
}

//절대경로 path에서 파일명만추출헤 fname에 저장하는 함수
void get_fname(char *path, char *fname)
{
	int i;
	for(i = strlen(path); i >= 0; i--){		//절대경로에서 파일명 추출
		if(path[i] == '/')
			break;
	}
	strcpy(fname, path + i + 1);
	return;
}

//file1과 file2가 같은 파일명인지 판단하는 함수
bool is_same_fname(char *file1, char *file2)
{
	char fname1[FILE_LEN], fname2[FILE_LEN];
	get_fname(file1, fname1);				//file1의 파일명 구함
	get_fname(file2, fname2);				//file2의 파일명 구함
	if(strcmp(fname1, fname2))				//두 파일 이름이 다를 경우 return false
		return false;
	return true;
}


//file1과 file2가 같은 파일인지 판단하는 함수
bool is_same_file(char *file1, char *file2)
{
	if(access(file1, F_OK) < 0)				//파일이 없을 경우
		return false;
	if(access(file2, F_OK) < 0)
		return false;
	if(!is_same_fname(file1, file2))		//두 파일의 파일명이 다를 경우
		return false;
	struct stat stat1, stat2;
	stat(file1, &stat1);
	stat(file2, &stat2);
	bool isDir1 = false, isDir2 = false;
	if(S_ISDIR(stat1.st_mode))
		isDir1 = true;
	if(S_ISDIR(stat2.st_mode))
		isDir2 = true;
	if(isDir1 != isDir2)					//두 파일의 종류(디렉터리, 일반파일)이 다를 경우
		return false;
	if(stat1.st_size != stat2.st_size)		//두 파일의 크기가 다를 경우
		return false;
	if(stat1.st_mtime != stat2.st_mtime)	//두 파일의 최종 수정 시간이 다를 경우
		return false;
	return true;
}

//상대경로 str의 절대경로를 path에 저장하는 함수
void get_abs_path(char *str, char *path)
{
	strcpy(path, cwdPath);	//현재 작업 디렉터리의 절대경로 구함
	strcat(path, "/");
	strcat(path, str);		//상대경로 연결
	return;
}

//디렉터리를 하위 파일 포함해 모두 삭제하는 함수
void remove_directory(char *path)
{
	DIR *dp = opendir(path);			//path 디렉터리 open
	struct dirent *now;
	while((now = readdir(dp)) != NULL){	//path 디렉터리 하위 파일들 탐색
		if(!strcmp(now->d_name, ".") || !strcmp(now->d_name, ".."))
			continue;
		char tmpPath[PATH_LEN];
		strcpy(tmpPath, path);
		strcat(tmpPath, "/");
		strcat(tmpPath, now->d_name);
		struct stat statbuf;
		stat(tmpPath, &statbuf);
		if(S_ISDIR(statbuf.st_mode))	//디렉터리일 경우 재귀 호출
			remove_directory(tmpPath);
		else{							//디렉터리 아닐 경우 remove
			if(!tOption){
				strcpy(logList[logCnt].path, tmpPath + strlen(dstPath));
				logList[logCnt++].isDeleted = true;
				remove(tmpPath);
			}
		}
	}
	remove(path);						//자기 자신 삭제
	return;
}

//srcFd의 파일을 dstFd에 복사하는 함수
void copy_file(int srcFd, int dstFd)
{
	char buf[BUF_LEN];
	int len = -1;
	while((len = read(srcFd, buf, BUF_LEN)) > 0)
		write(dstFd, buf, len);
	return;
}

//실제 sync 수행하는 함수
void do_sync(char *src, char *dst)
{
	struct stat srcStat;
	stat(src, &srcStat);
	struct utimbuf times;						//src의 최종 접근시간 및 수정시간 저장할 구조체
	times.actime = srcStat.st_atime;			//최종 접근 시간 src의 값으로 저장
	times.modtime = srcStat.st_mtime;			//최종 수정 시간 src의 값으로 저장
	if(!S_ISDIR(srcStat.st_mode)){				//src가 디렉터리가 아닌 파일인 경우
		char srcFname[FILE_LEN];
		char nowDstPath[PATH_LEN];				//dst에서의 src파일 절대경로 구함
		strcpy(nowDstPath, dst);
		strcat(nowDstPath, "/");
		get_fname(src, srcFname);
		strcat(nowDstPath, srcFname);
		int srcFd;
		if((srcFd = open(src, O_RDONLY)) < 0){
			fprintf(stderr, "file open error %s\n", src);
			exit(1);
		}
		int dstFd;
		if(!is_same_file(src, nowDstPath)){		//src와 dst의 파일이 다른 파일일 경우
			if((dstFd = open(nowDstPath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0){
				fprintf(stderr, "file open error %s\n", nowDstPath);
				exit(1);
			}

			char fname[FILE_LEN];
			get_fname(src, fname);					//파일명 획득
			strcpy(logList[logCnt].path, fname);	//logList에 src 파일 추가
			logList[logCnt].isDeleted = false;
			logList[logCnt++].size = srcStat.st_size;
			
			if(!tOption)
				copy_file(srcFd, dstFd);

			close(dstFd);

			utime(nowDstPath, &times);			//복사한 dst 파일의 최종 접근 시간 및 수정 시간 src와 같도록
		}
		close(srcFd);
		utime(src, &times);						//src 파일의 최종 접근 시간 및 수정 시간 src와 기존의 값과 같도록
		if(mOption){							//m Option인 경우
			DIR *dp = opendir(dst);				//dst 디렉터리 open
			struct dirent *now;
			while((now = readdir(dp)) != NULL){	//dst 디렉터리 하위 파일들 탐색
				if(!strcmp(now->d_name, ".") || !strcmp(now->d_name, ".."))
					continue;
				char tmpDstPath[PATH_LEN];
				strcpy(tmpDstPath, dst);
				strcat(tmpDstPath, "/");
				strcat(tmpDstPath, now->d_name);
				struct stat statbuf;
				stat(tmpDstPath, &statbuf);
				if(strcmp(srcFname, now->d_name)){	//src 파일과 다른 이름일 경우
					if(S_ISDIR(statbuf.st_mode))	//디렉터리일 경우 하위 파일까지 전부 remove
						remove_directory(tmpDstPath);
					else{							//일반 파일일 경우 remove
						if(!tOption){
							strcpy(logList[logCnt].path, tmpDstPath + strlen(dstPath) + 1);	//dst경로 기준 상대경로 path에 저장
							logList[logCnt++].isDeleted = true;
						}
						remove(tmpDstPath);
					}
				}
			}
		}
	}
	else {										//src가 디렉터리인 경우
		DIR *dp = opendir(src);
		struct dirent *now;
		while((now = readdir(dp)) != NULL)		//src 디렉터리 내 파일 읽기
		{
			if(!strcmp(now->d_name, ".") || !(strcmp(now->d_name, "..")))
				continue;
			char tmpSrcPath[PATH_LEN];			//src 디렉터리 내 파일의 절대경로
			strcpy(tmpSrcPath, src);
			strcat(tmpSrcPath, "/");
			strcat(tmpSrcPath, now->d_name);
			char tmpDstPath[PATH_LEN];			//dst 디렉터리 내 파일의 절대경로
			strcpy(tmpDstPath, dst);
			strcat(tmpDstPath, "/");
			strcat(tmpDstPath, now->d_name);
			struct stat statbuf;
			stat(tmpSrcPath, &statbuf);
			if(S_ISDIR(statbuf.st_mode)){						//now파일이 디렉터리라면
				if(rOption){									//rOption일 경우에는 재귀 호출
					if(access(tmpDstPath, F_OK) < 0)			//dst디렉터리에 해당 디렉터리가 없을 경우
						mkdir(tmpDstPath, 0755);				//새로 생성
					do_sync(tmpSrcPath, tmpDstPath);			//재귀 호출
				}
				else continue;									//rOption이 아닐 경우에는 다음 파일로 넘어감
			}
			else {												//now파일이 디렉터리가 아니라면
				if(access(tmpSrcPath, R_OK) < 0){
					fprintf(stderr, "file read error %s\n", tmpSrcPath);
					exit(1);
				}
				if(!is_same_file(tmpSrcPath, tmpDstPath)){		//두 파일이 다를 경우
					int tmpSrcFd, tmpDstFd;
					if((tmpSrcFd = open(tmpSrcPath, O_RDONLY)) < 0){
						fprintf(stderr, "file open error %s\n", tmpSrcPath);
						exit(1);
					}
					if(access(tmpDstPath, F_OK) >= 0){ 			//dst에 해당 파일이 존재한다면
						struct stat tmpDstStat;
						stat(tmpDstPath, &tmpDstStat);
						if(S_ISDIR(tmpDstStat.st_mode))			//dst의 같은 이름 가진 파일이 디렉터리일 경우 디렉터리 하위 파일까지 모두 삭제
							remove_directory(tmpDstPath);
					}
					if((tmpDstFd = open(tmpDstPath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0){
						fprintf(stderr, "file open error %s\n", tmpDstPath);
						exit(1);
					}
					if(!tOption)
						copy_file(tmpSrcFd, tmpDstFd);
					struct stat tmpSrcStat;
					stat(tmpSrcPath, &tmpSrcStat);
					strcpy(logList[logCnt].path, tmpSrcPath + strlen(srcPath) + 1);	//logList에 복사할 파일 추가
					logList[logCnt].isDeleted = false;
					logList[logCnt++].size = tmpSrcStat.st_size;
					close(tmpSrcFd);
					close(tmpDstFd);
					utime(tmpDstPath, &times);			//복사한 dst 파일의 최종 접근 시간 및 수정 시간 src와 같도록
					utime(tmpSrcPath, &times);			//src 파일의 최종 접근 시간 및 수정 시간 src와 기존의 값과 같도록
				}
			}
		}
		
		if(mOption){										//mOption일 경우
			DIR *dp = opendir(dst);
			struct dirent *now;
			while((now = readdir(dp)) != NULL)				//dst 디렉터리 내 파일 읽기
			{
				if(!strcmp(now->d_name, ".") || !(strcmp(now->d_name, "..")))
					continue;
				char tmpDstPath[PATH_LEN];					//dst 디렉터리 내 파일의 절대경로
				strcpy(tmpDstPath, dst);
				strcat(tmpDstPath, "/");
				strcat(tmpDstPath, now->d_name);
				char tmpSrcPath[PATH_LEN];					//src 디렉터리 내 파일의 절대경로
				strcpy(tmpSrcPath, src);
				strcat(tmpSrcPath, "/");
				strcat(tmpSrcPath, now->d_name);
				if(access(tmpSrcPath, F_OK) < 0){			//src 디렉터리 내 없는 파일인 경우
					struct stat tmpDstStat;
					stat(tmpDstPath, &tmpDstStat);
					if(S_ISDIR(tmpDstStat.st_mode))			//dst에 있는 파일이 디렉터리인 경우
						remove_directory(tmpDstPath); 		//하위 파일 포함 전부 삭제
					else {									//dst에 있는 파일이 일반파일인 경우
						if(!tOption){
							strcpy(logList[logCnt].path, tmpDstPath + strlen(dstPath) + 1);	//logList에 delete된 파일 추가
							logList[logCnt++].isDeleted = true;
						}
						remove(tmpDstPath);					//dst 디렉터리에서 파일 제거
					}
				}
			}
		}
	}
	return;
}

//tOption 실행하는 함수
void do_tOption(void)
{
	char compressCmd[TAR_CMD_LEN];
	chdir(srcPath);				//src로 cwd 이동
	strcpy(compressCmd, "tar -cvf ");
	strcat(compressCmd, TAR_FILE);
	for(int i = 0; i < logCnt; i++){
		strcat(compressCmd, " ");
		strcat(compressCmd, logList[i].path);
	}
	system(compressCmd);		//src에서 동기화 필요 파일들에 대한 압축 실행
	char extractCmd[CMD_LEN];
	strcpy(extractCmd, "tar -xvf ");
	strcat(extractCmd, TAR_FILE);
	strcat(extractCmd, " -C ");
	strcat(extractCmd, dstPath);
	system(extractCmd);			//dst에 압축 풀기
	struct stat statbuf;
	stat(TAR_FILE, &statbuf);
	tarSize = statbuf.st_size;
	remove(TAR_FILE);			//tar 파일 삭제
	chdir(cwdPath);				//cwdPath로 cwd 이동
	return;
}

//ssu_rsync의 main함수
int main(int argc, char *argv[])
{
	if(argc < 3){
		print_usage();
		exit(1);
	}
	getcwd(cwdPath, PATH_LEN);					//현재 작업 디렉터리의 절대경로 저장
	strcpy(srcPath, argv[argc-2]);
	strcpy(dstPath, argv[argc-1]);
	if(srcPath[0] != '/' && srcPath[0] != '~'){	//src 졀대경로로 변경
		char path [PATH_LEN];
		get_abs_path(srcPath, path);
		strcpy(srcPath, path);
	}
	if(dstPath[0] != '/' && dstPath[0] != '~'){	//dst 절대경로로 변경
		char path [PATH_LEN];
		get_abs_path(dstPath, path);
		strcpy(dstPath, path);
	}
	if(access(srcPath, F_OK) < 0){				//src 존재하지 않을 경우
		print_usage();
		exit(1);
	}
	if(access(dstPath, F_OK) < 0){			//dst 존재하지 않을 경우
		print_usage();
		exit(1);
	}
	struct stat srcStat, dstStat;
	stat(srcPath, &srcStat);
	stat(dstPath, &dstStat);
	if(access(srcPath, R_OK) < 0){			//src 읽기 권한 없을 경우
		print_usage();
		exit(1);
	}
	if(access(dstPath, R_OK) < 0){			//dst 읽기 권한 없을 경우
		print_usage();
		exit(1);
	}
	if(access(dstPath, W_OK) < 0){			//dst 쓰기 권한 없을 경우
		print_usage();
		exit(1);
	}
	if(S_ISDIR(srcStat.st_mode)){			//src가 디렉터리일 경우
		if(access(srcPath, X_OK) < 0){		//src가 디렉터리이면서 실행권한이 없을 경우
			print_usage();
			exit(1);
		}
	}
	if(!S_ISDIR(dstStat.st_mode)){			//dst가 디렉터리가 아닐 경우
		print_usage();
		exit(1);
	}
	if(access(dstPath, X_OK) < 0){			//dst가 디렉터리이면서 실행권한이 없을 경우
		print_usage();
		exit(1);
	}

	for(int i = 1; i < argc-2; i++){	//명령어 옵션 인자 파악
		if(argv[i][0] != '-')			//인자 첫글자가 -일 때에만 확인
			continue;
		int len = strlen(argv[i]);
		for(int j = 1; j < len; j++){	//각 옵션 찾기
			if(argv[i][j] == 'r')
				rOption = true;
			if(argv[i][j] == 't')
				tOption = true;
			if(argv[i][j] == 'm')
				mOption = true;
		}
	}

	memset(cmd, (char)0, CMD_LEN);
	if(argv[0][0] == '.' && argv[0][1] == '/')
		strcpy(cmd, argv[0]+2);
	for(int i = 1; i < argc; i++){		//실행 명령어 cmd에 저장
		strcat(cmd, " ");
		strcat(cmd, argv[i]);
	}

	if((logFd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0){	//ssu_rsync_log 파일 open
		fprintf(stderr, "log file open error\n");
		exit(1);
	}

	do_sync(srcPath, dstPath);

	if(tOption)
		do_tOption();

	add_log();

	close(logFd);

	exit(0);
}
