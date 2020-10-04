#include "header.h"

extern void time_to_customTime(time_t t_time, customTime *ct);
extern void customTime_to_str(customTime *ctm, char *str);
extern bool str_to_customTime(char *str, customTime *ct);
extern void copy_customTime(customTime *dst, customTime *src);
extern bool is_before_than_now(customTime *ct);
extern bool is_before(customTime *ct1, customTime *ct2);

extern void daemon_main(void);

extern void print_tree(FileNode *root, bool pipe[MAX_DEPTH], int depth);
extern void print_size_tree(FileNode *root, char path[PATH_LEN], int depth);
extern void make_tree(FileNode *node, struct dirent **fileList, int cnt, char *path);
extern void free_tree(FileNode *root);

char targetPath[PATH_LEN];
char trashFilesPath[PATH_LEN] = "../trash/files/";
char trashInfoPath[PATH_LEN] = "../trash/info/";

Node *head = NULL;		//linked list의 head

//linked list 비었는지 확인하는 함수
bool is_empty(void)
{
	return head == NULL;
}

//linked list에서 시간 순으로 정렬되도록 새로운 node 삽입하는 함수
void insert_node(Node *new)
{
	if(is_empty()){								//첫 node일 경우
		new->next = NULL;
		new->prev = NULL;
		head = new;
	}
	else {
		Node *now = head, *tail = NULL;
		while(now !=  NULL){						//head부터 마지막 node까지 반복
			if(is_before(&(now->ct), &(new->ct))){	//now보다 new가 더 이전이면
				if(now->next == NULL)
					tail = now;
				now = now->next;
			}
			else {									//now 위치에 node 삽입
				if(now == head){					//now가 head일 경우
					new->next = now;
					new->prev = NULL;
					now->prev = new;
					head = new;
				}
				else {								//now가 head가 아닐 경우
					new->prev = now->prev;
					new->next = now;
					now->prev->next = new;
					now->prev = new;
				}
				return;
			}
		}
		tail->next = new;							//linked list 마지막(tail)에 삽입될 경우
		new->prev = tail;
		new->next = NULL;
	}
	return;
}

//linked list에서 가장 빠른 시간의 node 삭제하는 함수
void delete_node(void)
{
	if(is_empty())				//linked list가 비었을 경우 삭제 X
		return;
	if(head->next == NULL){		//linked list node 1개일 경우
		free(head);
		head = NULL;
		return;
	}
	head = head->next;			//가장 최근 node (head) 삭제
	free(head->prev);
	head->prev = NULL;
	return;
}

// trash/info 내 파일들의 크기의 합이 2kb가 넘으면 가장 오래된 파일부터 하나씩 trash/file, trash/info에서 제거하는 함수
void check_info_size(void)
{
	off_t totalSize;		//전체 사이즈
	do{						//totalSize가 2kb보다 클 동안 do-while 반복
		struct dirent **fileList;
		int fileCnt = scandir(trashInfoPath, &fileList, NULL, alphasort);	// trash/info의 전체 파일 list 가져옴
		totalSize = 0;
		int oldestIdx = -1;
		time_t oldestTime = -1;
		off_t oldestSize = 0;
		for(int i = 2; i < fileCnt; i++){		// .와 .. 제외 전체 파일 탐색
			char path[PATH_LEN];
			int len = strlen(fileList[i]->d_name);
			for(int j = len-1; j >= 0; j--){
				if(fileList[i]->d_name[j] == '.')
					fileList[i]->d_name[j] = '\0';
				else break;
			}
			strcpy(path, trashInfoPath);
			strcat(path, fileList[i]->d_name);
			struct stat statbuf;
			stat(path, &statbuf);
			totalSize += statbuf.st_size;
			if((oldestIdx == -1 || oldestTime > statbuf.st_mtime) && (fileList[i]->d_name[0] != '.')){	//가장 오래된 file 찾기
				oldestIdx = i;
				oldestTime = statbuf.st_mtime;
				oldestSize = statbuf.st_size;
			}
		}
		if(totalSize >= MAX_INFO_SIZE){
			char oldestInfoPath[PATH_LEN];
			strcpy(oldestInfoPath, trashInfoPath);
			strcat(oldestInfoPath, fileList[oldestIdx]->d_name);
			remove(oldestInfoPath);			//가장 오래된 file의 trash/info 삭제

			char oldestFilePath[PATH_LEN];
			strcpy(oldestFilePath, trashFilesPath);
			strcat(oldestFilePath, fileList[oldestIdx]->d_name);
			remove(oldestFilePath);			//가장 오래된 file의 trash/file 삭제

			totalSize -= oldestSize;		//totalSize 감소
		}
	}while(totalSize >= MAX_INFO_SIZE);
	return;
}

//실제 delete 작업 수행 함수
void do_delete(char filePath[PATH_LEN], bool iOption, bool rOption)
{
	time_t now = time(NULL);
	customTime ct;
	time_to_customTime(now, &ct);			//현재 시각으로 customTime 생성
	if(access(filePath, F_OK) < 0)			//이미 해당 file이 없을 경우 종료 (같은 file에 대해 END_TIME이 여러 번 설정된 경우)
		return;
	char fname[NAME_LEN];
	int i;
	for(i = strlen(filePath); i >= 0; i--)	//절대경로에서 파일명만 추출
		if(filePath[i] == '/')
			break;
	strcpy(fname, filePath + i + 1);
	if(iOption){							//iOption일 경우 trash 생성 없이 바로 삭제
		remove(filePath);
		return;
	}
	char dst[PATH_LEN];						//파일 이동시킬 trash/files 경로 생성
	strcpy(dst, trashFilesPath);
	strcat(dst, fname);
	struct stat statbuf;
	stat(filePath, &statbuf);				//파일 이동 전 파일의 정보 저장

	char infoFile[PATH_LEN];				//info 파일 경로 생성
	strcpy(infoFile, trashInfoPath);
	strcat(infoFile, fname);

	int dupCnt = 0;										// trash/files 디렉터리 내 중복된 파일 개수
	if(access(dst, F_OK) >= 0){							// trash/files 내 동일한 이름이 있을 경우
		while(true){
			char tmpPath[PATH_LEN];
			sprintf(tmpPath, "%s_%d_", dst, ++dupCnt);	//파일명 뒤에 _숫자_ 붙인 경로 생성
			if(access(tmpPath, F_OK) < 0){				//만든 파일명 존재하지 않을 경우 (이번에 생성할 중복파일명일 경우)
				char tmp[8];
				sprintf(tmp, "_%d_", dupCnt);
				strcat(dst, tmp);						//trash/files 경로 갱신
				strcat(infoFile, tmp);					//trash/info 경로 갱신
				break;
			}
		}
	}

	if(rOption){
		printf("delete [y/n]? ");
		char c[10];
		scanf("%[^\n]", c);
		getchar();
		if(c[0] == 'n' || c[0] == 'N')									//n일 경우 종료
			return;
		else if(c[0] != 'y' && c[0] != 'Y')								//y가 아닐 경우 종료
			return;
	}

	rename(filePath, dst);	//파일 이동
	int infoFd = open(infoFile, O_RDWR | O_CREAT, 0644);	//info 파일 open
	char dtimeStr[LOG_LEN];
	strcpy(dtimeStr, "D : ");
	customTime_to_str(&ct, dtimeStr + 4);	//삭제 시간 문자열 생성
	char mtimeStr[LOG_LEN];
	strcpy(mtimeStr, "M : ");
	customTime mct;
	time_to_customTime(statbuf.st_mtime, &mct);	//이동전 파일의 최종 수정시간 customTime으로 생성
	customTime_to_str(&mct, mtimeStr + 4);	//최종 수정시간 문자열 생성
	write(infoFd, "[Trash Info]\n", strlen("[Trash Info]\n"));	//info 파일 작성 시작
	write(infoFd, filePath, strlen(filePath));	//절대경로 작성
	write(infoFd, "\n", 1);	//절대경로 작성
	write(infoFd, dtimeStr, strlen(dtimeStr));	//삭제시간 작성
	write(infoFd, "\n", 1);	//절대경로 작성
	write(infoFd, mtimeStr, strlen(mtimeStr));	//최종 수정시간 작성
	write(infoFd, "\n", 1);	//절대경로 작성
	close(infoFd);
	check_info_size();		//info의 전체 크기 2kb 초과하는지 확인
	return;
}

//SIGCHLD handler 함수
void sig_child_handler(int signum)
{
	if(signum != SIGCHLD)	//SIGCHLD가 아닐 경우 return
		return;
	if(is_empty())			//linked list가 비었을 경우 return (daemon_init process가 종료되어서 호출된 경우)
		return;
	do_delete(head->path, head->iOption, head->rOption);	//가장 빠른 시간의 node에 대해 delete 수행하도록 호출
	delete_node();			//가장 빠른 시간의 node linked list에서 삭제
	return;
}

//DELETE 명령 처리 함수
void cmd_delete(char cmdArgv[ARGV_MAX][NAME_LEN])
{
	char filePath[PATH_LEN];
	if(cmdArgv[1][0] == '~' || cmdArgv[1][0] == '/')	//FILENAME 절대경로일 경우
		strcpy(filePath, cmdArgv[1]);
	else {	//FILENAME 상대경로일 경우 절대경로로 변경
		getcwd(filePath, PATH_LEN);
		strcat(filePath, "/");
		strcat(filePath, cmdArgv[1]);
	}
	if(access(filePath, F_OK) < 0){	//파일 존재 여부 판단
		fprintf(stderr, "There is no '%s'\n", filePath);
		return;
	}
	bool endTime = false;	//ENDTIME 인자 있는지
	bool validTime = false;	//유효한 시간인지
	bool iOption = false;	//iOption인지
	bool rOption = false;	//rOption인지
	char timeStr[TIME_LEN + 5];	//시간 저장 문자열
	customTime ct;			//시간 저장 구조체
	for(int i = 2; i < ARGV_MAX; i++){	//2번째 인자부터 탐색
		if(strlen(cmdArgv[i]) == 0)
			continue;
		if(cmdArgv[i][0] == '-'){	//-로 시작하는 인자일 경우
			for(int j = 1; j < NAME_LEN; j++){
				if(cmdArgv[i][j] == 'i')	//i일 경우 iOption true
					iOption = true;
				else if(cmdArgv[i][j] == 'r')	//r일 경우 rOption true
					rOption = true;
			}
		}
		else{
			if(cmdArgv[i][0] >= '0' && cmdArgv[i][0] <= '9')
				if(i+1 < ARGV_MAX && cmdArgv[i+1][0] >= '0' && cmdArgv[i+1][0] <= '9'){	//i번째 인자와 i+1번째 인자가 모두 숫자로 시작할 경우
					strcpy(timeStr, cmdArgv[i]);
					strcat(timeStr, " ");
					strcat(timeStr, cmdArgv[i+1]);		//timeStr 완성
					validTime = str_to_customTime(timeStr, &ct);	//timeStr 이용해 customTime 완성
					endTime = true;
				}
		}
	}
	if(endTime && (!validTime || is_before_than_now(&ct))){	//현재보다 이전의 시간을 입력했다면 error
		fprintf(stderr, "ENDTIME error! %s\n", timeStr);
		return;
	}
	if(!endTime){									//ENDTIME을 입력하지 않았다면
		do_delete(filePath, iOption, rOption);		//즉시 수행 후 return
		return;
	}
	Node *node = (Node *)malloc(sizeof(Node) * 1);
	node->prev = NULL;
	node->next = NULL;
	strcpy(node->path, filePath);				//파일경로 복사
	copy_customTime(&(node->ct), &ct);			//customTime 복사
	node->iOption = iOption;
	node->rOption = rOption;
	insert_node(node);							//node 추가
	pid_t pid;
	if((pid = fork()) < 0)							//fork 실행
		fprintf(stderr, "delete fork error\n");
	else if(pid == 0){								//자식 process
		while(true){
			if(is_before_than_now(&ct))				//입력받은 ENDTIME이 현재보다 이전이거나 같다면
				exit(0);							//자식 process 종료 (부모 process에게 SIGCHLD 전달)
			sleep(1);
		}
	}
	return;
}

//SIZE 명령 처리 함수
void cmd_size(char cmdArgv[ARGV_MAX][NAME_LEN])
{
	bool dOption = false;
	int dNum = 0;
	char path[PATH_LEN];
	memset(path, (char)0, sizeof(path));
	strcpy(path, cmdArgv[1]);
	if(!strcmp(cmdArgv[2], "-d")){			//dOption 처리
		dOption = true;
		dNum = atoi(cmdArgv[3]);			//dNum 저장
		if(dNum <= 0 || dNum > MAX_DEPTH){
			fprintf(stderr, "Option error.  Number is %s.\n", cmdArgv[3]);
			return;
		}
	}
	chdir("..");							//check directory에서 현재 directory로 cd
	if(access(path, F_OK) < 0){				//실제 존재하는 file인지 확인
		fprintf(stderr, "There is no '%s'.\n", path);
		chdir(targetPath);			//check directory로 cd
		return;
	}
	FileNode *root = (FileNode *)malloc(sizeof(FileNode) * 1);
	make_tree(root, NULL, -1, path);		//tree 생성
	if(dOption)								//dOption일 경우
		print_size_tree(root, path, dNum);	//print_size_tree 호출
	else									//dOption 아닐 경우 root 정보만 출력
		printf("%ld\t\t./%s\n", root->size, path);
	free_tree(root);
	chdir(targetPath);				//check directory로 cd
	
	return;
}

//RECOVER 명령 처리 함수
void cmd_recover(char cmdArgv[ARGV_MAX][NAME_LEN])
{
	bool lOption = false;
	if(!strcmp(cmdArgv[2], "-l"))	//lOption 판별
		lOption = true;
	char infoPath[PATH_LEN];
	strcpy(infoPath, trashInfoPath);
	strcat(infoPath, cmdArgv[1]);
	int validIdx[MAX_DUP_CNT];						//중복 file 있는 index 저장 배열
	int cnt = 0;									//중복 file index 개수
	char dupInfoPath[MAX_DUP_CNT][PATH_LEN];		//중복 file 이름 저장 배열
	for(int i = 0; i < MAX_DUP_CNT; i++){			//중복 file 찾기
		char num[5];
		strcpy(dupInfoPath[i], infoPath);			//중복 file 경로 생성
		if(i != 0){									//0번째 중복 file은 이름 변경 X
			sprintf(num, "_%d_", i);
			strcat(dupInfoPath[i], num);			//중복 file 경로 완성
		}
		if(access(dupInfoPath[i], F_OK) >= 0)		//현재 file이 있을 경우
			validIdx[cnt++] = i;
	}
	if (cnt == 0) {									//찾은 중복 file이 하나도 없다면
		fprintf(stderr, "There is no '%s' in the 'trash' directory.\n", cmdArgv[1]);
		return;
	}
	char buf[BUFFER_SIZE];
	char recoverPath[MAX_DUP_CNT][PATH_LEN];		//info file들에서 읽어들인 절대 경로 저장 배열
	char mTimeStr[MAX_DUP_CNT][TIME_LEN+5];
	char dTimeStr[MAX_DUP_CNT][TIME_LEN+5];
	for(int i = 0; i < cnt; i++){											//같은 이름 갖는 info 파일 정보 모두 읽어들이기
		FILE *info = fopen(dupInfoPath[validIdx[i]], "r");
		memset(buf, (char) 0, sizeof(buf));
		fgets(buf, BUFFER_SIZE, info);										//1행 읽기
		fgets(recoverPath[validIdx[i]], PATH_LEN, info);					//2행 recoverPath에 읽기
		recoverPath[validIdx[i]][strlen(recoverPath[validIdx[i]])-1] = '\0';//개행문자 null문자로 교체
		fgets(dTimeStr[validIdx[i]], TIME_LEN+5, info);						//3행 dTimeStr에 읽기
		dTimeStr[validIdx[i]][strlen(dTimeStr[validIdx[i]])-1] = '\0';		//개행문자 null문자로 교체
		fgets(mTimeStr[validIdx[i]], TIME_LEN+5, info);						//4행 mTimeStr에 읽기
		mTimeStr[validIdx[i]][strlen(mTimeStr[validIdx[i]])-1] = '\0';		//개행문자 null문자로 교체
		fclose(info);
	}
	int select;
	if(cnt > 1){								//찾은 중복 file이 다수라면
		for(int i = 0; i < cnt; i++)			//중복 file 목록 출력
			printf("%2d: %s\t%s %s\n", i + 1, cmdArgv[1], dTimeStr[validIdx[i]], mTimeStr[validIdx[i]]);
		printf("Choose: ");
		scanf("%d", &select);					//중복 file 중 선택 입력 받기
		getchar();
		if(select < 1 || select > cnt){		//select 범위 check
			fprintf(stderr, "Choose error.\n");
			return;
		}
		select = validIdx[select-1];			//select를 실제 idx값으로 변경
	}
	else										//찾은 중복 file이 한 개라면 (중복 file이 아니라면)
		select = validIdx[0];
	char recoverDir[BUFFER_SIZE];				//recover할 Directory 절대경로
	strcpy(recoverDir, recoverPath[select]);
	int len = strlen(recoverDir);
	for(int i = len-1; i >= 0; i--){			//절대경로에서 마지막 파일명만 제거
		if(recoverDir[i] == '/'){
			recoverDir[i] = '\0';
			break;
		}
		else recoverDir[i] = '\0';
	}
	if(access(recoverDir, F_OK)){			//recover할 Directory가 존재하지 않으면
		fprintf(stderr, "There is no '%s' directory. Can't Recover '%s'.\n", recoverDir, cmdArgv[1]);
		return;
	}
	char srcFilePath[PATH_LEN];				//trash/file의 절대 경로 생성
	strcpy(srcFilePath, trashFilesPath);
	strcat(srcFilePath, cmdArgv[1]);
	if(select != 0){
		char num[5];
		sprintf(num, "_%d_", select);
		strcat(srcFilePath, num);
	}

	char dstPath[PATH_LEN];					//file 옮길 경로
	strcpy(dstPath, recoverPath[select]);	//info 파일에 저장되어 있던 기존 경로 복사
	len = strlen(dstPath);
	if(access(dstPath, F_OK) >= 0){			//이미 dstPath와 같은 file이 존재한다면
		for(int i = len; i >= 0; i--){		//dstPath에서 파일명 제거
			if(dstPath[i] == '/')
				break;
			else
				dstPath[i] = '\0';
		}
		len = strlen(dstPath);				//파일명 제외한 절대경로 길이 저장
		int i;
		for(i = 1; i < MAX_DUP_CNT; i++){
			char tmpName[NAME_LEN];			//새로운 파일명 생성
			sprintf(tmpName, "%d_", i);
			strcat(tmpName, cmdArgv[1]);
			strcpy(dstPath + len, tmpName);	//dstPath에서 파일명 변경
			if(access(dstPath, F_OK) < 0)	//dstPath와 같은 file이 존재하지 않는다면 break
				break;
		}
		if(i == MAX_DUP_CNT)				//중복된 파일이 이미 너무 많을 경우 return
			return;
	}
	if(lOption){	//lOption 처리
		struct dirent **fileList;
		int fileCnt = scandir(trashInfoPath, &fileList, NULL, alphasort);
		chdir(trashInfoPath);		//trash/info로 cd
		char fnameList[MAX_INFO_CNT][NAME_LEN];
		char dTimeList[MAX_INFO_CNT][TIME_LEN];
		for(int i = 2; i < fileCnt; i++){	// . .. 제외 모든 파일 정보 획득
			struct stat statbuf;
			stat(fileList[i]->d_name, &statbuf);
			strcpy(fnameList[i-2], fileList[i]->d_name);	//파일명 저장
			char buf[BUFFER_SIZE];
			FILE *info = fopen(fileList[i]->d_name, "r");
			memset(buf, (char) 0, sizeof(buf));
			fgets(buf, BUFFER_SIZE, info);					//1행 읽기
			memset(buf, (char) 0, sizeof(buf));
			fgets(buf, BUFFER_SIZE, info);					//2행 읽기
			memset(buf, (char) 0, sizeof(buf));
			fgets(buf, BUFFER_SIZE, info);					//3행 읽기
			fclose(info);
			buf[strlen(buf)-1] = '\0';						//개행문자 null문자로 교체
			strcpy(dTimeList[i-2], buf+4) ;					//buf에서 D : 제거 후 dTimeList에 복사
		}
		chdir("../..");
		chdir(targetPath);	//check로 cd
		fileCnt -= 2;
		for(int i = 0; i < fileCnt-1; i++){		//dTime 기준으로 bubble sort
			for(int j = 0; j < fileCnt-1-i; j++){
				customTime ct1, ct2;
				str_to_customTime(dTimeList[j], &ct1);
				str_to_customTime(dTimeList[j+1], &ct2);
				if(is_before(&ct2, &ct1)){
					char tmpTime[TIME_LEN];
					strcpy(tmpTime, dTimeList[j]);
					strcpy(dTimeList[j], dTimeList[j+1]);
					strcpy(dTimeList[j+1], tmpTime);
					char tmpName[NAME_LEN];
					strcpy(tmpName, fnameList[j]);
					strcpy(fnameList[j], fnameList[j+1]);
					strcpy(fnameList[j+1], tmpName);
				}
			}
		}
		for(int i = 0; i < fileCnt; i++){		//출력
			int len = strlen(fnameList[i]);
			if(fnameList[i][len-1] == '_'){		//중복 file일 경우
				fnameList[i][len-1] = '\0';
				int j = len - 2;
				while(fnameList[i][j] != '_')
					fnameList[i][j--] = '\0';
				fnameList[i][j] = '\0';
			}
			printf("%2d. %s\t\t%s\n", i+1, fnameList[i], dTimeList[i]);
		}
	}
	rename(srcFilePath, dstPath);			// trash/file에서 원래 경로로 이동
	remove(dupInfoPath[select]);			// trash/info에서 제거
	return;
}

//TREE 명령 처리 함수
void cmd_tree(void)
{
	chdir("./..");					//부모 디렉터리로 이동
	char path[PATH_LEN];
	strcpy(path, targetPath);
	FileNode *root = (FileNode *)malloc(sizeof(FileNode) * 1);
	make_tree(root, NULL, -1, path);//Tree 생성
	bool pipe[MAX_DEPTH];
	print_tree(root, pipe, 0);		//Tree 출력
	free_tree(root);				//Tree Free
	chdir(path);					//check 디렉터리로 다시 이동
	return;
}

//EXIT 명령 처리 함수
void cmd_exit(void)
{
	int dp = open(DAEMON_PID_FILE, O_RDONLY, 0644);	// Working Directory의 .daemonPid 파일 open
	char buf[10];
	read(dp, buf, sizeof(buf));
	pid_t daemonPid = atoi(buf);	//daemon의 pid 획득
	kill(daemonPid, SIGKILL);		//daemon process kill
	printf("monitoring is finished...\n");
	close(dp);
	remove(DAEMON_PID_FILE);		// .daemonPid 파일 제거
	exit(0);
}

//HELP 명령 처리 함수
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

//알파벳 소문자를 대문자로 변경하는 함수
void make_upper(char *str)
{
	int len = strlen(str);
	for(int i = 0; i < len; i++){
		if(str[i] >= 'a' && str[i] <= 'z')
			str[i] -= 'a'-'A';
	}
}

//명령어를 공백 기준으로 tokenizing하는 함수
void tokenize_command(char *command, char cmdArgv[ARGV_MAX][NAME_LEN])
{
	memset(cmdArgv, (char)0, ARGV_MAX*NAME_LEN*sizeof(char));	//배열 0으로 초기화
	char *p = strtok(command, " ");	//공백 기준으로 token 구분
	int argCnt = 0;	//구분된 token 개수
	do{
		strcpy(cmdArgv[argCnt], p);
		int len = strlen(cmdArgv[argCnt]);
		for(int i = 0; i < len; i++){	//공백문자를 null문자로 변경
			if(cmdArgv[argCnt][i] <= ' '){
				cmdArgv[argCnt][i] = '\0';
				break;
			}
		}
		p = strtok(NULL, " ");	//다음 token 찾기
		if(strlen(cmdArgv[argCnt]) > 0)	//현재 명령이 유효할 경우(길이 0초과일 경우)
			argCnt++;
	}while(p != NULL);	//다음 공백 없을 때까지 반복
	return;
}

//입력받은 명령어 tokenize 및 실행하는 함수
void execute_command(char *command)
{
	char cmdArgv[ARGV_MAX][NAME_LEN];	//명령어 인자 단위로 저장할 배열
	tokenize_command(command, cmdArgv);	//명령어 인자별로 구분
	make_upper(cmdArgv[0]);	//첫번째 인자 (명령어)의 소문자 모두 대문자로 변경
	enum CMD_CASE cmdCase;	//명령어 종류 열거형
	if(strlen(cmdArgv[0]) == 0)	//명령이 없을 경우 NONE
		cmdCase = NONE;
	else if(!strcmp(cmdArgv[0], "DELETE"))
		cmdCase = DELETE;
	else if(!strcmp(cmdArgv[0], "SIZE"))
		cmdCase = SIZE;
	else if(!strcmp(cmdArgv[0], "RECOVER"))
		cmdCase = RECOVER;
	else if(!strcmp(cmdArgv[0], "TREE"))
		cmdCase = TREE;
	else if(!strcmp(cmdArgv[0], "EXIT"))
		cmdCase = EXIT;
	else if(!strcmp(cmdArgv[0], "HELP"))
		cmdCase = HELP;
	else	//그 외의 경우 HELP
		cmdCase = HELP;
	switch(cmdCase){	//명령어 종류 따라 알맞은 명령어 실행 함수 호출
		case NONE:
			break;
		case DELETE:
			cmd_delete(cmdArgv);
			break;
		case SIZE:
			cmd_size(cmdArgv);
			break;
		case RECOVER:
			cmd_recover(cmdArgv);
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
	}
	return;
}

//명령 prompt 출력 및 명령어 입력 / 실행 함수
void prompt(void)
{
	char *promptStr = PROMPT_STRING;
	char command[CMD_BUFFER];
	while(true){	//명령 prompt 반복 출력
		fputs(promptStr, stdout);
		fgets(command, sizeof(command), stdin);	//명령어 입력받음
		execute_command(command);	//입력받은 명령어 실행
	}
	return;
}

int main(int argc, char *argv[])
{
	if(argc != 2){
		fprintf(stderr, "Usage\t: %s [TARGET_DIRECTORY_PATH]\n", argv[0]);
		exit(1);
	}
	//trash directory 생성
	mkdir("trash", 0755);
	mkdir("trash/files", 0755);
	mkdir("trash/info", 0755);

	signal(SIGCHLD, sig_child_handler);	//자식 process 종료 시의 handler 등록

	strcpy(targetPath, argv[1]);
	//check directory 생성, 이미 있을 경우 생성X
	mkdir(targetPath, 0755);
	chdir(targetPath);	//cheeck directory로 이동

	//daemon process 수행용 자식 process 생성
	pid_t pid;
	if((pid = fork()) < 0){
		fprintf(stderr, "daemon start error\n");
		exit(1);
	}
	//daemon process 생성용 자식 process
	else if(pid == 0)
		daemon_main();	//daemon process 실행
	//부모 process
	else{
		prompt();	//명령 prompt 실행
	}
	exit(0);
}
