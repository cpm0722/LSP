#include "header.h"

// time.c
extern void time_to_custom_time(time_t t_time, CustomTime *ct);
extern void custom_time_to_str(CustomTime *ctm, char *str);
extern bool str_to_custom_time(char *str, CustomTime *ct);
extern void copy_custom_time(CustomTime *dst, CustomTime *src);
extern bool is_before_than_now(CustomTime *ct);
extern bool is_before(CustomTime *ct1, CustomTime *ct2);
// daemon.c
extern void daemon_main(void);
// tree.c
extern void print_tree(FileNode *root, bool pipe[MAX_DEPTH], int depth);
extern void print_size_tree(FileNode *root, char path[PATH_LEN], int depth);
extern void make_tree(FileNode *node, struct dirent **file_list, int cnt, char *path);
extern void free_tree(FileNode *root);

char g_target_path[PATH_LEN];
char g_trash_files_path[PATH_LEN] = "../trash/files/";
char g_trash_info_path[PATH_LEN] = "../trash/info/";
Node *g_head = NULL;    // linked list의 head

// linked list 비었는지 확인하는 함수
bool is_empty(void)
{
  return g_head == NULL;
}

// linked list에서 시간 순으로 정렬되도록 새로운 node 삽입하는 함수
void insert_node(Node *new)
{
  if (is_empty()) {                // 첫 node일 경우
    new->next = NULL;
    new->prev = NULL;
    g_head = new;
  }
  else {
    Node *now = g_head, *tail = NULL;
    while (now !=  NULL) {            // head부터 마지막 node까지 반복
      if (is_before(&(now->ct), &(new->ct))) {  // now보다 new가 더 이전이면
        if (now->next == NULL) {
          tail = now;
        }
        now = now->next;
      }
      else {                  // now 위치에 node 삽입
        if (now == g_head) {          // now가 head일 경우
          new->next = now;
          new->prev = NULL;
          now->prev = new;
          g_head = new;
        }
        else {                // now가 head가 아닐 경우
          new->prev = now->prev;
          new->next = now;
          now->prev->next = new;
          now->prev = new;
        }
        return;
      }
    }
    tail->next = new;              // linked list 마지막(tail)에 삽입될 경우
    new->prev = tail;
    new->next = NULL;
  }
  return;
}

// linked list에서 가장 빠른 시간의 node 삭제하는 함수
void delete_node(void)
{
  if (is_empty()) {        // linked list가 비었을 경우 삭제 X
    return;
  }
  if (g_head->next == NULL) {    // linked list node 1개일 경우
    free(g_head);
    g_head = NULL;
    return;
  }
  g_head = g_head->next;      // 가장 최근 node (head) 삭제
  free(g_head->prev);
  g_head->prev = NULL;
  return;
}

// trash/info 내 파일들의 크기의 합이 2kb가 넘으면 가장 오래된 파일부터 하나씩 trash/file, trash/info에서 제거하는 함수
void check_info_size(void)
{
  off_t total_size;    // 전체 사이즈
  do {            // total_size가 2kb보다 클 동안 do-while 반복
    struct dirent **file_list;
    int file_cnt = scandir(g_trash_info_path, &file_list, NULL, alphasort);  // trash/info의 전체 파일 list 가져옴
    total_size = 0;
    int oldest_idx = -1;
    time_t oldest_time = -1;
    off_t oldest_size = 0;
    for (int i = 2; i < file_cnt; i++) {    // .와 .. 제외 전체 파일 탐색
      char path[PATH_LEN];
      int len = strlen(file_list[i]->d_name);
      for (int j = len-1; j >= 0; j--) {
        if (file_list[i]->d_name[j] == '.') {
          file_list[i]->d_name[j] = '\0';
        }
        else {
          break;
        }
      }
      strcpy(path, g_trash_info_path);
      strcat(path, file_list[i]->d_name);
      struct stat statbuf;
      stat(path, &statbuf);
      total_size += statbuf.st_size;
      if ((oldest_idx == -1 || oldest_time > statbuf.st_mtime) && (file_list[i]->d_name[0] != '.')) {  // 가장 오래된 file 찾기
        oldest_idx = i;
        oldest_time = statbuf.st_mtime;
        oldest_size = statbuf.st_size;
      }
    }
    if (total_size >= MAX_INFO_SIZE) {
      char oldest_info_path[PATH_LEN];
      strcpy(oldest_info_path, g_trash_info_path);
      strcat(oldest_info_path, file_list[oldest_idx]->d_name);
      remove(oldest_info_path);      // 가장 오래된 file의 trash/info 삭제
      char oldest_file_path[PATH_LEN];
      strcpy(oldest_file_path, g_trash_files_path);
      strcat(oldest_file_path, file_list[oldest_idx]->d_name);
      remove(oldest_file_path);      // 가장 오래된 file의 trash/file 삭제
      total_size -= oldest_size;    // total_size 감소
    }
  } while (total_size >= MAX_INFO_SIZE);
  return;
}

// 실제 delete 작업 수행 함수
void do_delete(char file_path[PATH_LEN], bool option_i, bool option_r)
{
  time_t now = time(NULL);
  CustomTime ct;
  time_to_custom_time(now, &ct);      // 현재 시각으로 CustomTime 생성
  if (access(file_path, F_OK) < 0) {      // 이미 해당 file이 없을 경우 종료 (같은 file에 대해 END_TIME이 여러 번 설정된 경우)
    return;
  }
  char fname[NAME_LEN];
  int i;
  for (i = strlen(file_path); i >= 0; i--) {  // 절대경로에서 파일명만 추출
    if (file_path[i] == '/') {
      break;
    }
  }
  strcpy(fname, file_path + i + 1);
  if (option_i) {              // option_i일 경우 trash 생성 없이 바로 삭제
    remove(file_path);
    return;
  }
  char dst[PATH_LEN];            // 파일 이동시킬 trash/files 경로 생성
  strcpy(dst, g_trash_files_path);
  strcat(dst, fname);
  struct stat statbuf;
  stat(file_path, &statbuf);        // 파일 이동 전 파일의 정보 저장
  char info_file[PATH_LEN];        // info 파일 경로 생성
  strcpy(info_file, g_trash_info_path);
  strcat(info_file, fname);
  int dup_cnt = 0;                    // trash/files 디렉터리 내 중복된 파일 개수
  if (access(dst, F_OK) >= 0) {              // trash/files 내 동일한 이름이 있을 경우
    while (true) {
      char tmp_path[PATH_LEN];
      sprintf(tmp_path, "%s_%d_", dst, ++dup_cnt);  // 파일명 뒤에 _숫자_ 붙인 경로 생성
      if (access(tmp_path, F_OK) < 0) {        // 만든 파일명 존재하지 않을 경우 (이번에 생성할 중복파일명일 경우)
        char tmp[8];
        sprintf(tmp, "_%d_", dup_cnt);
        strcat(dst, tmp);            // trash/files 경로 갱신
        strcat(info_file, tmp);          // trash/info 경로 갱신
        break;
      }
    }
  }
  if (option_r) {
    printf("delete [y/n]? ");
    char c[10];
    scanf("%[^\n]", c);
    getchar();
    if (c[0] == 'n' || c[0] == 'N') {                  // n일 경우 종료
      return;
    }
    else if (c[0] != 'y' && c[0] != 'Y') {                // y가 아닐 경우 종료
      return;
    }
  }
  rename(file_path, dst);  // 파일 이동
  int info_fd = open(info_file, O_RDWR | O_CREAT, 0644);  // info 파일 open
  char dtime_str[LOG_LEN];
  strcpy(dtime_str, "D : ");
  custom_time_to_str(&ct, dtime_str + 4);  // 삭제 시간 문자열 생성
  char mtime_str[LOG_LEN];
  strcpy(mtime_str, "M : ");
  CustomTime mct;
  time_to_custom_time(statbuf.st_mtime, &mct);  // 이동전 파일의 최종 수정시간 CustomTime으로 생성
  custom_time_to_str(&mct, mtime_str + 4);  // 최종 수정시간 문자열 생성
  write(info_fd, "[Trash Info]\n", strlen("[Trash Info]\n"));  // info 파일 작성 시작
  write(info_fd, file_path, strlen(file_path));  // 절대경로 작성
  write(info_fd, "\n", 1);  // 절대경로 작성
  write(info_fd, dtime_str, strlen(dtime_str));  // 삭제시간 작성
  write(info_fd, "\n", 1);  // 절대경로 작성
  write(info_fd, mtime_str, strlen(mtime_str));  // 최종 수정시간 작성
  write(info_fd, "\n", 1);  // 절대경로 작성
  close(info_fd);
  check_info_size();    // info의 전체 크기 2kb 초과하는지 확인
  return;
}

// SIGCHLD handler 함수
void sig_child_handler(int signum)
{
  if (signum != SIGCHLD) {  // SIGCHLD가 아닐 경우 return
    return;
  }
  if (is_empty()) {      // linked list가 비었을 경우 return (daemon_init process가 종료되어서 호출된 경우)
    return;
  }
  do_delete(g_head->path, g_head->option_i, g_head->option_r);  // 가장 빠른 시간의 node에 대해 delete 수행하도록 호출
  delete_node();      // 가장 빠른 시간의 node linked list에서 삭제
  return;
}

// DELETE 명령 처리 함수
void cmd_delete(char cmd_argv[ARGV_MAX][NAME_LEN])
{
  char file_path[PATH_LEN];
  if (cmd_argv[1][0] == '~' || cmd_argv[1][0] == '/') {  // FILENAME 절대경로일 경우
    strcpy(file_path, cmd_argv[1]);
  }
  else {  // FILENAME 상대경로일 경우 절대경로로 변경
    getcwd(file_path, PATH_LEN);
    strcat(file_path, "/");
    strcat(file_path, cmd_argv[1]);
  }
  if (access(file_path, F_OK) < 0) {  // 파일 존재 여부 판단
    fprintf(stderr, "There is no '%s'\n", file_path);
    return;
  }
  bool ent_time = false;  // ENDTIME 인자 있는지
  bool valid_time = false;  // 유효한 시간인지
  bool option_i = false;  // option_i인지
  bool option_r = false;  // option_r인지
  char time_str[TIME_LEN + 5];  // 시간 저장 문자열
  CustomTime ct;      // 시간 저장 구조체
  for (int i = 2; i < ARGV_MAX; i++) {  // 2번째 인자부터 탐색
    if (strlen(cmd_argv[i]) == 0) {
      continue;
    }
    if (cmd_argv[i][0] == '-') {  // -로 시작하는 인자일 경우
      for (int j = 1; j < NAME_LEN; j++) {
        if (cmd_argv[i][j] == 'i') {  // i일 경우 option_i true
          option_i = true;
        }
        else if (cmd_argv[i][j] == 'r') {  // r일 경우 option_r true
          option_r = true;
        }
      }
    }
    else {
      if (cmd_argv[i][0] >= '0' && cmd_argv[i][0] <= '9') {
        if (i+1 < ARGV_MAX && cmd_argv[i+1][0] >= '0' && cmd_argv[i+1][0] <= '9') {  // i번째 인자와 i+1번째 인자가 모두 숫자로 시작할 경우
          strcpy(time_str, cmd_argv[i]);
          strcat(time_str, " ");
          strcat(time_str, cmd_argv[i+1]);    // time_str 완성
          valid_time = str_to_custom_time(time_str, &ct);  // time_str 이용해 CustomTime 완성
          ent_time = true;
        }
      }
    }
  }
  if (ent_time && (!valid_time || is_before_than_now(&ct))) {  // 현재보다 이전의 시간을 입력했다면 error
    fprintf(stderr, "ENDTIME error! %s\n", time_str);
    return;
  }
  if (!ent_time) {                  // ENDTIME을 입력하지 않았다면
    do_delete(file_path, option_i, option_r);    // 즉시 수행 후 return
    return;
  }
  Node *node = (Node *)malloc(sizeof(Node) * 1);
  node->prev = NULL;
  node->next = NULL;
  strcpy(node->path, file_path);        // 파일경로 복사
  copy_custom_time(&(node->ct), &ct);      // CustomTime 복사
  node->option_i = option_i;
  node->option_r = option_r;
  insert_node(node);              // node 추가
  pid_t pid;
  if ((pid = fork()) < 0) {              // fork 실행
    fprintf(stderr, "delete fork error\n");
  }
  else if (pid == 0) {                // 자식 process
    while (true) {
      if (is_before_than_now(&ct)) {        // 입력받은 ENDTIME이 현재보다 이전이거나 같다면
        exit(0);              // 자식 process 종료 (부모 process에게 SIGCHLD 전달)
      }
      sleep(1);
    }
  }
  return;
}

// SIZE 명령 처리 함수
void cmd_size(char cmd_argv[ARGV_MAX][NAME_LEN])
{
  bool option_d = false;
  int d_num = 0;
  char path[PATH_LEN];
  memset(path, (char)0, sizeof(path));
  strcpy(path, cmd_argv[1]);
  if (!strcmp(cmd_argv[2], "-d")) {      // option_d 처리
    option_d = true;
    d_num = atoi(cmd_argv[3]);      // d_num 저장
    if (d_num <= 0 || d_num > MAX_DEPTH) {
      fprintf(stderr, "Option error.  Number is %s.\n", cmd_argv[3]);
      return;
    }
  }
  chdir("..");              // check directory에서 현재 directory로 cd
  if (access(path, F_OK) < 0) {        // 실제 존재하는 file인지 확인
    fprintf(stderr, "There is no '%s'.\n", path);
    chdir(g_target_path);      // check directory로 cd
    return;
  }
  FileNode *root = (FileNode *)malloc(sizeof(FileNode) * 1);
  make_tree(root, NULL, -1, path);    // tree 생성
  if (option_d) {                // option_d일 경우
    print_size_tree(root, path, d_num);  // print_size_tree 호출
  }
  else {                  // option_d 아닐 경우 root 정보만 출력
    printf("%ld\t\t./%s\n", root->size, path);
  }
  free_tree(root);
  chdir(g_target_path);        // check directory로 cd
  return;
}

// RECOVER 명령 처리 함수
void cmd_recover(char cmd_argv[ARGV_MAX][NAME_LEN])
{
  bool option_l = false;
  if (!strcmp(cmd_argv[2], "-l")) {  // option_l 판별
    option_l = true;
  }
  char info_path[PATH_LEN];
  strcpy(info_path, g_trash_info_path);
  strcat(info_path, cmd_argv[1]);
  int valid_idx[MAX_DUP_CNT];            // 중복 file 있는 index 저장 배열
  int cnt = 0;                  // 중복 file index 개수
  char dup_info_path[MAX_DUP_CNT][PATH_LEN];    // 중복 file 이름 저장 배열
  for (int i = 0; i < MAX_DUP_CNT; i++) {      // 중복 file 찾기
    char num[5];
    strcpy(dup_info_path[i], info_path);      // 중복 file 경로 생성
    if (i != 0) {                  // 0번째 중복 file은 이름 변경 X
      sprintf(num, "_%d_", i);
      strcat(dup_info_path[i], num);      // 중복 file 경로 완성
    }
    if (access(dup_info_path[i], F_OK) >= 0) {    // 현재 file이 있을 경우
      valid_idx[cnt++] = i;
    }
  }
  if (cnt == 0) {                  // 찾은 중복 file이 하나도 없다면
    fprintf(stderr, "There is no '%s' in the 'trash' directory.\n", cmd_argv[1]);
    return;
  }
  char buf[BUFFER_SIZE];
  char recover_path[MAX_DUP_CNT][PATH_LEN];    // info file들에서 읽어들인 절대 경로 저장 배열
  char m_time_str[MAX_DUP_CNT][TIME_LEN+5];
  char d_time_str[MAX_DUP_CNT][TIME_LEN+5];
  for (int i = 0; i < cnt; i++) {                      // 같은 이름 갖는 info 파일 정보 모두 읽어들이기
    FILE *info = fopen(dup_info_path[valid_idx[i]], "r");
    memset(buf, (char) 0, sizeof(buf));
    fgets(buf, BUFFER_SIZE, info);                    // 1행 읽기
    fgets(recover_path[valid_idx[i]], PATH_LEN, info);          // 2행 recover_path에 읽기
    recover_path[valid_idx[i]][strlen(recover_path[valid_idx[i]])-1] = '\0';// 개행문자 null문자로 교체
    fgets(d_time_str[valid_idx[i]], TIME_LEN+5, info);            // 3행 d_time_str에 읽기
    d_time_str[valid_idx[i]][strlen(d_time_str[valid_idx[i]])-1] = '\0';    // 개행문자 null문자로 교체
    fgets(m_time_str[valid_idx[i]], TIME_LEN+5, info);            // 4행 m_time_str에 읽기
    m_time_str[valid_idx[i]][strlen(m_time_str[valid_idx[i]])-1] = '\0';    // 개행문자 null문자로 교체
    fclose(info);
  }
  int select;
  if (cnt > 1) {                // 찾은 중복 file이 다수라면
    for (int i = 0; i < cnt; i++) {      // 중복 file 목록 출력
      printf("%2d: %s\t%s %s\n", i + 1, cmd_argv[1], d_time_str[valid_idx[i]], m_time_str[valid_idx[i]]);
    }
    printf("Choose: ");
    scanf("%d", &select);          // 중복 file 중 선택 입력 받기
    getchar();
    if (select < 1 || select > cnt) {    // select 범위 check
      fprintf(stderr, "Choose error.\n");
      return;
    }
    select = valid_idx[select-1];      // select를 실제 idx값으로 변경
  }
  else {                    // 찾은 중복 file이 한 개라면 (중복 file이 아니라면)
    select = valid_idx[0];
  }
  char recover_dir[BUFFER_SIZE];        // recover할 Directory 절대경로
  strcpy(recover_dir, recover_path[select]);
  int len = strlen(recover_dir);
  for (int i = len-1; i >= 0; i--) {      // 절대경로에서 마지막 파일명만 제거
    if (recover_dir[i] == '/') {
      recover_dir[i] = '\0';
      break;
    }
    else {
    }recover_dir[i] = '\0';
  }
  if (access(recover_dir, F_OK)) {      // recover할 Directory가 존재하지 않으면
    fprintf(stderr, "There is no '%s' directory. Can't Recover '%s'.\n", recover_dir, cmd_argv[1]);
    return;
  }
  char src_file_path[PATH_LEN];        // trash/file의 절대 경로 생성
  strcpy(src_file_path, g_trash_files_path);
  strcat(src_file_path, cmd_argv[1]);
  if (select != 0) {
    char num[5];
    sprintf(num, "_%d_", select);
    strcat(src_file_path, num);
  }

  char dst_file_path[PATH_LEN];          // file 옮길 경로
  strcpy(dst_file_path, recover_path[select]);  // info 파일에 저장되어 있던 기존 경로 복사
  len = strlen(dst_file_path);
  if (access(dst_file_path, F_OK) >= 0) {      // 이미 dst_file_path와 같은 file이 존재한다면
    for (int i = len; i >= 0; i--) {    // dst_file_path에서 파일명 제거
      if (dst_file_path[i] == '/') {
        break;
      }
      else {
        dst_file_path[i] = '\0';
      }
    }
    len = strlen(dst_file_path);        // 파일명 제외한 절대경로 길이 저장
    int i;
    for (i = 1; i < MAX_DUP_CNT; i++) {
      char tmpName[NAME_LEN];      // 새로운 파일명 생성
      sprintf(tmpName, "%d_", i);
      strcat(tmpName, cmd_argv[1]);
      strcpy(dst_file_path + len, tmpName);  // dst_file_path에서 파일명 변경
      if (access(dst_file_path, F_OK) < 0) {  // dst_file_path와 같은 file이 존재하지 않는다면 break
        break;
      }
    }
    if (i == MAX_DUP_CNT) {        // 중복된 파일이 이미 너무 많을 경우 return
      return;
    }
  }
  if (option_l) {  // option_l 처리
    struct dirent **file_list;
    int file_cnt = scandir(g_trash_info_path, &file_list, NULL, alphasort);
    chdir(g_trash_info_path);    // trash/info로 cd
    char fname_list[MAX_INFO_CNT][NAME_LEN];
    char d_time_list[MAX_INFO_CNT][TIME_LEN];
    for (int i = 2; i < file_cnt; i++) {  // . .. 제외 모든 파일 정보 획득
      struct stat statbuf;
      stat(file_list[i]->d_name, &statbuf);
      strcpy(fname_list[i-2], file_list[i]->d_name);  // 파일명 저장
      char buf[BUFFER_SIZE];
      FILE *info = fopen(file_list[i]->d_name, "r");
      memset(buf, (char) 0, sizeof(buf));
      fgets(buf, BUFFER_SIZE, info);          // 1행 읽기
      memset(buf, (char) 0, sizeof(buf));
      fgets(buf, BUFFER_SIZE, info);          // 2행 읽기
      memset(buf, (char) 0, sizeof(buf));
      fgets(buf, BUFFER_SIZE, info);          // 3행 읽기
      fclose(info);
      buf[strlen(buf)-1] = '\0';            // 개행문자 null문자로 교체
      strcpy(d_time_list[i-2], buf+4) ;          // buf에서 D : 제거 후 d_time_list에 복사
    }
    chdir("../..");
    chdir(g_target_path);  // check로 cd
    file_cnt -= 2;
    for (int i = 0; i < file_cnt-1; i++) {    // dTime 기준으로 bubble sort
      for (int j = 0; j < file_cnt-1-i; j++) {
        CustomTime ct1, ct2;
        str_to_custom_time(d_time_list[j], &ct1);
        str_to_custom_time(d_time_list[j+1], &ct2);
        if (is_before(&ct2, &ct1)) {
          char tmpTime[TIME_LEN];
          strcpy(tmpTime, d_time_list[j]);
          strcpy(d_time_list[j], d_time_list[j+1]);
          strcpy(d_time_list[j+1], tmpTime);
          char tmpName[NAME_LEN];
          strcpy(tmpName, fname_list[j]);
          strcpy(fname_list[j], fname_list[j+1]);
          strcpy(fname_list[j+1], tmpName);
        }
      }
    }
    for (int i = 0; i < file_cnt; i++) {    // 출력
      int len = strlen(fname_list[i]);
      if (fname_list[i][len-1] == '_') {    // 중복 file일 경우
        fname_list[i][len-1] = '\0';
        int j = len - 2;
        while (fname_list[i][j] != '_') {
          fname_list[i][j--] = '\0';
        }
        fname_list[i][j] = '\0';
      }
      printf("%2d. %s\t\t%s\n", i+1, fname_list[i], d_time_list[i]);
    }
  }
  rename(src_file_path, dst_file_path);      // trash/file에서 원래 경로로 이동
  remove(dup_info_path[select]);      // trash/info에서 제거
  return;
}

// TREE 명령 처리 함수
void cmd_tree(void)
{
  chdir("./..");          // 부모 디렉터리로 이동
  char path[PATH_LEN];
  strcpy(path, g_target_path);
  FileNode *root = (FileNode *)malloc(sizeof(FileNode) * 1);
  make_tree(root, NULL, -1, path);// Tree 생성
  bool pipe[MAX_DEPTH];
  print_tree(root, pipe, 0);    // Tree 출력
  free_tree(root);        // Tree Free
  chdir(path);          // check 디렉터리로 다시 이동
  return;
}

// EXIT 명령 처리 함수
void cmd_exit(void)
{
  int dp = open(DAEMON_PID_FILE, O_RDONLY, 0644);  // Working Directory의 .daemonPid 파일 open
  char buf[10];
  read(dp, buf, sizeof(buf));
  pid_t daemonPid = atoi(buf);  // daemon의 pid 획득
  kill(daemonPid, SIGKILL);    // daemon process kill
  printf("monitoring is finished...\n");
  close(dp);
  remove(DAEMON_PID_FILE);    // .daemonPid 파일 제거
  exit(0);
}

// HELP 명령 처리 함수
void cmd_help(void)
{
  printf("\n[DELETE]\n");
  printf("Usage      : delete <FILE_NAME> <END_TIME> [OPTION]\n");
  printf("Descript   : Delete the <FILE_NAME> at <END_TIME>.\n");
  printf("<END_TIME> : YYYY-MM-DD hh-mm(-ss)\n");
  printf("Option     : \n");
  printf("-i           Don't use trash directory.\n");
  printf("-r           Ask at END_TIME.\n\n");
  printf("\n[SIZE]\n");
  printf("Usage      : size <FILE_NAME> [OPTION]\n");
  printf("Descript   : Print <FILE_NAME>'s path and size.\n");
  printf("Option     : \n");
  printf("-d <NUMBER>  print child directory until depth is <NUMBER>.\n\n");
  printf("\n[RECOVER]\n");
  printf("Usage      : recover <FILE_NAME> [OPTION]\n");
  printf("Descript   : Recover trash <FILE_NAME>.\n");
  printf("Option     : \n");
  printf("-l           print all trash files sorted by deleted-time.\n\n");
  printf("\n[TREE]\n");
  printf("Usage      : tree\n");
  printf("Descript   : Print check directory and child directory as tree structure.\n\n");
  printf("\n[EXIT]\n");
  printf("Usage      : exit\n");
  printf("Descript   : Terminate ssu_mntr.\n\n");
  printf("\n[HELP]\n");
  printf("Usage      : help\n");
  printf("Descrpit   : Print how to use ssu_mntr.\n\n");
  return;
}

// 알파벳 소문자를 대문자로 변경하는 함수
void make_upper(char *str)
{
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    if (str[i] >= 'a' && str[i] <= 'z') {
      str[i] -= 'a'-'A';
    }
  }
}

// 명령어를 공백 기준으로 tokenizing하는 함수
void tokenize_command(char *command, char cmd_argv[ARGV_MAX][NAME_LEN])
{
  memset(cmd_argv, (char)0, ARGV_MAX*NAME_LEN*sizeof(char));  // 배열 0으로 초기화
  char *p = strtok(command, " ");  // 공백 기준으로 token 구분
  int arg_cnt = 0;  // 구분된 token 개수
  do {
    strcpy(cmd_argv[arg_cnt], p);
    int len = strlen(cmd_argv[arg_cnt]);
    for (int i = 0; i < len; i++) {  // 공백문자를 null문자로 변경
      if (cmd_argv[arg_cnt][i] <= ' ') {
        cmd_argv[arg_cnt][i] = '\0';
        break;
      }
    }
    p = strtok(NULL, " ");  // 다음 token 찾기
    if (strlen(cmd_argv[arg_cnt]) > 0) { // 현재 명령이 유효할 경우(길이 0초과일 경우)
      arg_cnt++;
    }
  } while (p != NULL);  // 다음 공백 없을 때까지 반복
  return;
}

// 입력받은 명령어 tokenize 및 실행하는 함수
void execute_command(char *command)
{
  char cmd_argv[ARGV_MAX][NAME_LEN];  // 명령어 인자 단위로 저장할 배열
  tokenize_command(command, cmd_argv);  // 명령어 인자별로 구분
  make_upper(cmd_argv[0]);  // 첫번째 인자 (명령어)의 소문자 모두 대문자로 변경
  CmdCase cmd_case;  // 명령어 종류 열거형
  if (strlen(cmd_argv[0]) == 0) {  // 명령이 없을 경우 NONE
    cmd_case = NONE;
  }
  else if (!strcmp(cmd_argv[0], "DELETE")) {
    cmd_case = DELETE;
  }
  else if (!strcmp(cmd_argv[0], "SIZE")) {
    cmd_case = SIZE;
  }
  else if (!strcmp(cmd_argv[0], "RECOVER")) {
    cmd_case = RECOVER;
  }
  else if (!strcmp(cmd_argv[0], "TREE")) {
    cmd_case = TREE;
  }
  else if (!strcmp(cmd_argv[0], "EXIT")) {
    cmd_case = EXIT;
  }
  else if (!strcmp(cmd_argv[0], "HELP")) {
    cmd_case = HELP;
  }
  else { // 그 외의 경우 HELP
    cmd_case = HELP;
  }
  switch (cmd_case) {  // 명령어 종류 따라 알맞은 명령어 실행 함수 호출
    case NONE:
      break;
    case DELETE:
      cmd_delete(cmd_argv);
      break;
    case SIZE:
      cmd_size(cmd_argv);
      break;
    case RECOVER:
      cmd_recover(cmd_argv);
      break;
    case TREE:
      cmd_tree();
      break;
    case EXIT:
      cmd_exit();
      break;
    case HELP:
      cmd_help();
      break;
    default:
      break;
  }
  return;
}

// 명령 prompt 출력 및 명령어 입력 / 실행 함수
void prompt(void)
{
  char *prompt_str = PROMPT_STRING;
  char command[CMD_BUFFER];
  while (true) {  // 명령 prompt 반복 출력
    fputs(prompt_str, stdout);
    fgets(command, sizeof(command), stdin);  // 명령어 입력받음
    execute_command(command);  // 입력받은 명령어 실행
  }
  return;
}

int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Usage\t: %s [TARGET_DIRECTORY_PATH]\n", argv[0]);
    exit(1);
  }
  // trash directory 생성
  mkdir("trash", 0755);
  mkdir("trash/files", 0755);
  mkdir("trash/info", 0755);
  signal(SIGCHLD, sig_child_handler);  // 자식 process 종료 시의 handler 등록
  strcpy(g_target_path, argv[1]);
  // check directory 생성, 이미 있을 경우 생성X
  mkdir(g_target_path, 0755);
  chdir(g_target_path);  // cheeck directory로 이동
  // daemon process 수행용 자식 process 생성
  pid_t pid;
  if ((pid = fork()) < 0) {
    fprintf(stderr, "daemon start error\n");
    exit(1);
  }
  // daemon process 생성용 자식 process
  else if (pid == 0) {
    daemon_main();  // daemon process 실행
  }
  // 부모 process
  else {
    prompt();  // 명령 prompt 실행
  }
  exit(0);
}
