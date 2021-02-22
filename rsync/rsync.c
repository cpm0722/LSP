#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
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

typedef enum {false, true} bool;

struct timeval start_time, end_time;  // 실행시간 측정
unsigned long long proc_time;         // 실행시간 측정
char cwd_path[PATH_LEN];              // 작업 디렉터리 절대 경로
char src_path[PATH_LEN];              // src 디렉터리 절대 경로
char dst_path[PATH_LEN];              // dst 디렉터리 절대 경로
char cmd[CMD_LEN];                    // 초기에 입력했던 명령어
int log_fd;
bool option_r = false;
bool option_t = false;
bool option_m = false;
struct {
  char path[PATH_LEN];
  int size;
  bool is_deleted;
} log_list[LOG_MAX];                  // log에 기록할 파일 list 저장
int log_cnt = 0;                      // log에 기록할 파일 개수
int tar_size = 0;                     // option_t 시 생성한 tar 파일의 크기

// 사용법을 출력하는 함수
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

// 현재 시간 정보 문자열에 저장하는 함수
void make_time_str(char *str)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  strftime(str, TIME_LEN, "%c", tm);
  return;
}

// log 추가하는 함수
void add_log(void)
{
  char buf[BUF_LEN];
  char time_str[TIME_LEN];
  make_time_str(time_str);
  sprintf(buf, "[%s] %s\n", time_str, cmd); // 시간 및 명령어 write
  write(log_fd, buf, strlen(buf));
  if (!option_t) {
    for (int i = 0; i < log_cnt; i++) {     // 각 파일들에 대한 동기화 정보 write
      memset(buf, (char)0, BUF_LEN);
      if (!log_list[i].is_deleted) {
        sprintf(buf, "\t%s\t%dbytes\n", log_list[i].path, log_list[i].size);
      }
      else {
        sprintf(buf, "\t%s\tdelete\n", log_list[i].path);
      }
      write(log_fd, buf, strlen(buf));
    }
  }
  else {
    memset(buf, (char)0, BUF_LEN);
    sprintf(buf, "\ttotalSize %d\n", tar_size);
    write(log_fd, buf, strlen(buf));
    for (int i = 0; i < log_cnt; i++) { // 각 파일들 상대경로 write
      memset(buf, (char)0, BUF_LEN);
      sprintf(buf, "\t%s\n", log_list[i].path);
      write(log_fd, buf, strlen(buf));
    }
  }
  return;
}

// 절대경로 path에서 파일명만추출헤 fname에 저장하는 함수
void get_fname(char *path, char *fname)
{
  int i;
  for (i = strlen(path); i >= 0; i--) {    // 절대경로에서 파일명 추출
    if (path[i] == '/') {
      break;
    }
  }
  strcpy(fname, path + i + 1);
  return;
}

// file1과 file2가 같은 파일명인지 판단하는 함수
bool is_same_fname(char *file1, char *file2)
{
  char fname1[FILE_LEN], fname2[FILE_LEN];
  get_fname(file1, fname1);     // file1의 파일명 구함
  get_fname(file2, fname2);     // file2의 파일명 구함
  if (strcmp(fname1, fname2)) {
    return false;
  }
  return true;
}


// file1과 file2가 같은 파일인지 판단하는 함수
bool is_same_file(char *file1, char *file2)
{
  if (access(file1, F_OK) < 0) {          // 파일이 없을 경우
    return false;
  }
  if (access(file2, F_OK) < 0) {
    return false;
  }
  if (!is_same_fname(file1, file2)) {     // 두 파일의 파일명이 다를 경우
    return false;
  }
  struct stat stat1, stat2;
  stat(file1, &stat1);
  stat(file2, &stat2);
  bool is_dir1 = false, is_dir2 = false;
  if (S_ISDIR(stat1.st_mode)) {
    is_dir1 = true;
  }
  if (S_ISDIR(stat2.st_mode)) {
    is_dir2 = true;
  }
  if (is_dir1 != is_dir2) {               // 두 파일의 종류(디렉터리, 일반파일)이 다를 경우
    return false;
  }
  if (stat1.st_size != stat2.st_size) {   // 두 파일의 크기가 다를 경우
    return false;
  }
  if (stat1.st_mtime != stat2.st_mtime) { // 두 파일의 최종 수정 시간이 다를 경우
    return false;
  }
  return true;
}

// 상대경로 str의 절대경로를 path에 저장하는 함수
void get_abs_path(char *str, char *path)
{
  strcpy(path, cwd_path); // 현재 작업 디렉터리의 절대경로 구함
  strcat(path, "/");
  strcat(path, str);      // 상대경로 연결
  return;
}

// 디렉터리를 하위 파일 포함해 모두 삭제하는 함수
void remove_directory(char *path)
{
  DIR *dp = opendir(path);
  struct dirent *now;
  while ((now = readdir(dp)) != NULL) {  // path 디렉터리 하위 파일들 탐색
    if (!strcmp(now->d_name, ".") || !strcmp(now->d_name, "..")) {
      continue;
    }
    char tmp_path[PATH_LEN];
    strcpy(tmp_path, path);
    strcat(tmp_path, "/");
    strcat(tmp_path, now->d_name);
    struct stat statbuf;
    stat(tmp_path, &statbuf);
    if (S_ISDIR(statbuf.st_mode)) { // 디렉터리일 경우 재귀 호출
      remove_directory(tmp_path);
    }
    else {  // 디렉터리 아닐 경우 remove
      if (!option_t) {
        strcpy(log_list[log_cnt].path, tmp_path + strlen(dst_path));
        log_list[log_cnt++].is_deleted = true;
        remove(tmp_path);
      }
    }
  }
  remove(path); // 자기 자신 삭제
  return;
}

// src_fd의 파일을 dst_fd에 복사하는 함수
void copy_file(int src_fd, int dst_fd)
{
  char buf[BUF_LEN];
  int len = -1;
  while ((len = read(src_fd, buf, BUF_LEN)) > 0) {
    write(dst_fd, buf, len);
  }
  return;
}

// 실제 sync 수행하는 함수
void execute_sync(char *src, char *dst)
{
  struct stat src_stat;
  stat(src, &src_stat);
  struct utimbuf times; // src의 최종 접근시간 및 수정시간 저장할 구조체
  times.actime = src_stat.st_atime;   // 최종 접근 시간 src의 값으로 저장
  times.modtime = src_stat.st_mtime;  // 최종 수정 시간 src의 값으로 저장
  if (!S_ISDIR(src_stat.st_mode)) {   // src가 디렉터리가 아닌 파일인 경우
    char src_fname[FILE_LEN];
    char now_dst_path[PATH_LEN];      // dst에서의 src파일 절대경로 구함
    strcpy(now_dst_path, dst);
    strcat(now_dst_path, "/");
    get_fname(src, src_fname);
    strcat(now_dst_path, src_fname);
    int src_fd;
    if ((src_fd = open(src, O_RDONLY)) < 0) {
      fprintf(stderr, "file open error %s\n", src);
      exit(1);
    }
    int dst_fd;
    if (!is_same_file(src, now_dst_path)) {    // src와 dst의 파일이 다른 파일일 경우
      if ((dst_fd = open(now_dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        fprintf(stderr, "file open error %s\n", now_dst_path);
        exit(1);
      }
      char fname[FILE_LEN];
      get_fname(src, fname);                  // 파일명 획득
      strcpy(log_list[log_cnt].path, fname);  // log_list에 src 파일 추가
      log_list[log_cnt].is_deleted = false;
      log_list[log_cnt++].size = src_stat.st_size;
      if (!option_t) {
        copy_file(src_fd, dst_fd);
      }
      close(dst_fd);
      utime(now_dst_path, &times);  // 복사한 dst 파일의 최종 접근 시간 및 수정 시간 src와 같도록
    }
    close(src_fd);
    utime(src, &times); // src 파일의 최종 접근 시간 및 수정 시간 src와 기존의 값과 같도록
    if (option_m) {
      DIR *dp = opendir(dst);
      struct dirent *now;
      while ((now = readdir(dp)) != NULL) { // dst 디렉터리 하위 파일들 탐색
        if (!strcmp(now->d_name, ".") || !strcmp(now->d_name, "..")) {
          continue;
        }
        char tmp_dst_path[PATH_LEN];
        strcpy(tmp_dst_path, dst);
        strcat(tmp_dst_path, "/");
        strcat(tmp_dst_path, now->d_name);
        struct stat statbuf;
        stat(tmp_dst_path, &statbuf);
        if (strcmp(src_fname, now->d_name)) {
          if (S_ISDIR(statbuf.st_mode)) { // 디렉터리일 경우 하위 파일까지 전부 remove
            remove_directory(tmp_dst_path);
          }
          else {  // 일반 파일일 경우 remove
            if (!option_t) {
              // dst경로 기준 상대경로 path에 저장
              strcpy(log_list[log_cnt].path, tmp_dst_path + strlen(dst_path) + 1);
              log_list[log_cnt++].is_deleted = true;
            }
            remove(tmp_dst_path);
          }
        }
      }
    }
  }
  else {  // src가 디렉터리인 경우
    DIR *dp = opendir(src);
    struct dirent *now;
    while ((now = readdir(dp)) != NULL) { // src 디렉터리 내 파일 읽기
      if (!strcmp(now->d_name, ".") || !(strcmp(now->d_name, ".."))) {
        continue;
      }
      char tmp_src_path[PATH_LEN];      // src 디렉터리 내 파일의 절대경로
      strcpy(tmp_src_path, src);
      strcat(tmp_src_path, "/");
      strcat(tmp_src_path, now->d_name);
      char tmp_dst_path[PATH_LEN];      // dst 디렉터리 내 파일의 절대경로
      strcpy(tmp_dst_path, dst);
      strcat(tmp_dst_path, "/");
      strcat(tmp_dst_path, now->d_name);
      struct stat statbuf;
      stat(tmp_src_path, &statbuf);
      if (S_ISDIR(statbuf.st_mode)) {   // now파일이 디렉터리라면
        if (option_r) {                 // option_r일 경우에는 재귀 호출
          if (access(tmp_dst_path, F_OK) < 0) { // dst디렉터리에 해당 디렉터리가 없을 경우
            mkdir(tmp_dst_path, 0755);  // 디렉터리 새로 생성
          }
          execute_sync(tmp_src_path, tmp_dst_path); // 재귀 호출
        }
        else {  // option_r이 아닐 경우에는 다음 파일로 넘어감
          continue;
        }
      }
      else {  // now파일이 디렉터리가 아니라면
        if (access(tmp_src_path, R_OK) < 0) {
          fprintf(stderr, "file read error %s\n", tmp_src_path);
          exit(1);
        }
        if (!is_same_file(tmp_src_path, tmp_dst_path)) {  // 두 파일이 다를 경우
          int tmpSrcFd, tmpDstFd;
          if ((tmpSrcFd = open(tmp_src_path, O_RDONLY)) < 0) {
            fprintf(stderr, "file open error %s\n", tmp_src_path);
            exit(1);
          }
          if (access(tmp_dst_path, F_OK) >= 0) {  // dst에 해당 파일이 존재한다면
            struct stat tmp_dst_stat;
            stat(tmp_dst_path, &tmp_dst_stat);
            // dst의 같은 이름 가진 파일이 디렉터리일 경우 디렉터리 하위 파일까지 모두 삭제
            if (S_ISDIR(tmp_dst_stat.st_mode)) {
              remove_directory(tmp_dst_path);
            }
          }
          if ((tmpDstFd = open(tmp_dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
            fprintf(stderr, "file open error %s\n", tmp_dst_path);
            exit(1);
          }
          if (!option_t) {
            copy_file(tmpSrcFd, tmpDstFd);
          }
          struct stat tmpSrcStat;
          stat(tmp_src_path, &tmpSrcStat);
          // log_list에 복사할 파일 추가
          strcpy(log_list[log_cnt].path, tmp_src_path + strlen(src_path) + 1);
          log_list[log_cnt].is_deleted = false;
          log_list[log_cnt++].size = tmpSrcStat.st_size;
          close(tmpSrcFd);
          close(tmpDstFd);
          utime(tmp_dst_path, &times);  // 복사한 dst 파일의 최종 접근 시간 및 수정 시간 src와 같도록
          utime(tmp_src_path, &times);  // src 파일의 최종 접근 시간 및 수정 시간 src와 기존의 값과 같도록
        }
      }
    }
    if (option_m) {
      DIR *dp = opendir(dst);
      struct dirent *now;
      while ((now = readdir(dp)) != NULL) {     // dst 디렉터리 내 파일 읽기
        if (!strcmp(now->d_name, ".") || !(strcmp(now->d_name, ".."))) {
          continue;
        }
        char tmp_dst_path[PATH_LEN];  // dst 디렉터리 내 파일의 절대경로
        strcpy(tmp_dst_path, dst);
        strcat(tmp_dst_path, "/");
        strcat(tmp_dst_path, now->d_name);
        char tmp_src_path[PATH_LEN];  // src 디렉터리 내 파일의 절대경로
        strcpy(tmp_src_path, src);
        strcat(tmp_src_path, "/");
        strcat(tmp_src_path, now->d_name);
        if (access(tmp_src_path, F_OK) < 0) {   // src 디렉터리 내 없는 파일인 경우
          struct stat tmp_dst_stat;
          stat(tmp_dst_path, &tmp_dst_stat);
          if (S_ISDIR(tmp_dst_stat.st_mode)) {  // dst에 있는 파일이 디렉터리인 경우
            remove_directory(tmp_dst_path);     // 하위 파일 포함 전부 삭제
          }
          else {  // dst에 있는 파일이 일반파일인 경우
            if (!option_t) {
              strcpy(log_list[log_cnt].path, tmp_dst_path + strlen(dst_path) + 1);  // log_list에 delete된 파일 추가
              log_list[log_cnt++].is_deleted = true;
            }
            remove(tmp_dst_path); // dst 디렉터리에서 파일 제거
          }
        }
      }
    }
  }
  return;
}

// option_t 실행하는 함수
void execute_option_t(void)
{
  char compress_cmd[TAR_CMD_LEN];
  chdir(src_path);        // src로 cwd 이동
  strcpy(compress_cmd, "tar -cvf ");
  strcat(compress_cmd, TAR_FILE);
  for (int i = 0; i < log_cnt; i++) {
    strcat(compress_cmd, " ");
    strcat(compress_cmd, log_list[i].path);
  }
  system(compress_cmd);    // src에서 동기화 필요 파일들에 대한 압축 실행
  char extract_cmd[CMD_LEN];
  strcpy(extract_cmd, "tar -xvf ");
  strcat(extract_cmd, TAR_FILE);
  strcat(extract_cmd, " -C ");
  strcat(extract_cmd, dst_path);
  system(extract_cmd);     // dst에 압축 풀기
  struct stat statbuf;
  stat(TAR_FILE, &statbuf);
  tar_size = statbuf.st_size;
  remove(TAR_FILE);       // tar 파일 삭제
  chdir(cwd_path);        // cwd_path로 cwd 이동
  return;
}

// ssu_rsync의 main함수
int main(int argc, char *argv[])
{
  if (argc < 3) {
    print_usage();
    exit(1);
  }
  getcwd(cwd_path, PATH_LEN); // 현재 작업 디렉터리의 절대경로 저장
  strcpy(src_path, argv[argc-2]);
  strcpy(dst_path, argv[argc-1]);
  char path [PATH_LEN];
  if (src_path[0] != '/' && src_path[0] != '~') {  // src 졀대경로로 변경
    get_abs_path(src_path, path);
    strcpy(src_path, path);
  }
  if (dst_path[0] != '/' && dst_path[0] != '~') {  // dst 절대경로로 변경
    get_abs_path(dst_path, path);
    strcpy(dst_path, path);
  }
  if (access(src_path, F_OK) < 0) {   // src 존재하지 않을 경우
    print_usage();
    exit(1);
  }
  if (access(dst_path, F_OK) < 0) {   // dst 존재하지 않을 경우
    print_usage();
    exit(1);
  }
  struct stat src_stat, dst_stat;
  stat(src_path, &src_stat);
  stat(dst_path, &dst_stat);
  if (access(src_path, R_OK) < 0) {   // src 읽기 권한 없을 경우
    print_usage();
    exit(1);
  }
  if (access(dst_path, R_OK) < 0) {   // dst 읽기 권한 없을 경우
    print_usage();
    exit(1);
  }
  if (access(dst_path, W_OK) < 0) {   // dst 쓰기 권한 없을 경우
    print_usage();
    exit(1);
  }
  if (S_ISDIR(src_stat.st_mode)) {    // src가 디렉터리일 경우
    if (access(src_path, X_OK) < 0) { // src가 디렉터리이면서 실행권한이 없을 경우
      print_usage();
      exit(1);
    }
  }
  if (!S_ISDIR(dst_stat.st_mode)) {   // dst가 디렉터리가 아닐 경우
    print_usage();
    exit(1);
  }
  if (access(dst_path, X_OK) < 0) {   // dst가 디렉터리이면서 실행권한이 없을 경우
    print_usage();
    exit(1);
  }
  for (int i = 1; i < argc-2; i++) {  // 명령어 옵션 인자 파악
    if (argv[i][0] != '-') {          // 인자 첫글자가 -일 때에만 확인
      continue;
    }
    int len = strlen(argv[i]);
    for (int j = 1; j < len; j++) {   // 각 옵션 찾기
      if (argv[i][j] == 'r') {
        option_r = true;
      }
      if (argv[i][j] == 't') {
        option_t = true;
      }
      if (argv[i][j] == 'm') {
        option_m = true;
      }
    }
  }
  memset(cmd, (char)0, CMD_LEN);
  if (argv[0][0] == '.' && argv[0][1] == '/') {
    strcpy(cmd, argv[0]+2);
  }
  for (int i = 1; i < argc; i++) {    // 실행 명령어 cmd에 저장
    strcat(cmd, " ");
    strcat(cmd, argv[i]);
  }
  if ((log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0) {  // ssu_rsync_log 파일 open
    fprintf(stderr, "log file open error\n");
    exit(1);
  }
  execute_sync(src_path, dst_path);
  if (option_t) {
    execute_option_t();
  }
  add_log();
  close(log_fd);
  exit(0);
}
