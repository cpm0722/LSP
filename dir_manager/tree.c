#include "header.h"

extern void add_log(enum FILE_STATUS status, char *fname);

//FileNode를 Tree 구조로 출력하는 함수
void print_tree(FileNode *root, bool pipe[MAX_DEPTH], int depth)
{
	unsigned char space[TREE_NAME_LEN] = {"                     "};
	if(depth == MAX_DEPTH){
		putchar('\n');
		return;
	}
	//file 이름 문자열 생성
	char file[NAME_LEN];
	strcpy(file, root->name);
	//자식이 있을 때에는 -를, 없을 때에는 공백을 저장
	for(int i = strlen(file); i < NAME_LEN-1; i++){
		if(root->childCnt <= 0)
			file[i] = ' ';
		else
			file[i] = '-';
	}
	file[NAME_LEN-1] = '\0';
	if(depth == 0)
		printf("--");
	printf(" %s", file);
	if(root->childCnt <= 0)
		putchar('\n');
	for(int i = 0; i < root->childCnt; i++){
		if(i != 0){
			//공백 문자열 출력
			putchar(' ');
			for(int j = 0; j < depth; j++){
				printf("%s", space);
				if(pipe[depth])
					putchar('|');
			}
			printf("%s", space);
		}
		putchar('|');
		putchar('-');
		pipe[depth + 1] = true;
		print_tree(root->child[i], pipe, depth + 1);
		pipe[depth + 1] = false;
	}
	if(depth == 0)
		putchar('\n');
	return;
}

//size 명령어에서 -d option 사용 시 출력하는 함수
void print_size_tree(FileNode *root, char path[PATH_LEN], int depth)
{
	printf("%ld\t\t./%s\n", root->size, path);
	if(depth == 1){
		return;
	}
	for(int i = 0; i < root->childCnt; i++){				//자식들 탐색
		char childPath[PATH_LEN];
		strcpy(childPath, path);
		strcat(childPath, "/");
		strcat(childPath, root->child[i]->name);
		print_size_tree(root->child[i], childPath, depth - 1);	//자식들에 대해 재귀 호출
	}
	return;
}

//path의 FileNode 생성하는 함수
void make_tree(FileNode *node, struct dirent **fileList, int cnt, char *path)
{
	//root Node일 경우
	if(cnt == -1){
		node->parent = NULL;
		strcpy(node->name, path);
		struct stat statbuf;
		stat(path, &statbuf);
		if(S_ISDIR(statbuf.st_mode)){	//디렉터리일 때
			//directory내 file들 정보 읽어온 뒤 개수 저장
			int tmp = scandir(path, &fileList, NULL, alphasort);
			//.과 .. 제외
			if(tmp != 0)
				tmp -= 2;
			if(tmp < 0)
				tmp = 0;
			node->childCnt = tmp;
		}
		else							//디렉터리가 아닌 파일일 때
			node->childCnt = -1;
	}
	if(node->childCnt == -1){			//디렉터리가 아닌 파일일 경우
		node->child = NULL;
		struct stat statbuf;
		stat(path, &statbuf);
		node->size = statbuf.st_size;	//자기 자신의 size 저장
		return;
	}
	//directory내 file 개수만큼 FileNode* child에 생성
	node->child = (FileNode **)malloc(sizeof(FileNode *) * node->childCnt);
	// .과 .. 제외하며 file들 탐색
	for(int i = 0; i < node->childCnt + 2; i++){
		if(!strcmp(fileList[i]->d_name, ".") || !strcmp(fileList[i]->d_name, ".."))
			continue;
		//새로운 FileNode 생성
		node->child[i-2] = (FileNode *)malloc(sizeof(FileNode) * 1);
		//이름 복사
		strcpy(node->child[i-2]->name, fileList[i]->d_name);
		//부모 node 연결
		node->child[i-2]->parent = node;
		//자식 node list null로 초기화
		node->child[i-2]->child = NULL;
		//현재 경로 + file경로 생성
		char childPath[PATH_LEN];
		strcpy(childPath, path);
		strcat(childPath, "/");
		strcat(childPath, fileList[i]->d_name);
		struct stat statbuf;
		//파일 정보 불러옴
		stat(childPath, &statbuf);
		//최종 수정 시간 저장
		node->child[i-2]->mtime = statbuf.st_mtime;
		struct dirent **childFileList;
		//file 하위에 file이 더 있는지 확인
		int cnt = scandir(childPath, &childFileList, NULL, alphasort);
		// .과 .. 제외
		if(cnt != 0)
			cnt -= 2;
		if(cnt< 0)
			cnt = 0;
		if(S_ISDIR(statbuf.st_mode))		//디렉터리인 경우
			node->child[i-2]->childCnt = cnt;
		else								//디렉터리가 아닌 파일인 경우
			node->child[i-2]->childCnt = -1;
		//하위 file들에 대해 재귀 호출
		make_tree(node->child[i-2], childFileList, cnt, childPath);
	}
	node->size = 0;
	for(int i = 0; i < node->childCnt; i++)	//자기자신의 size = 자식들의 size의 합
		node->size += node->child[i]->size;
	return;
}

//FileNodes Tree 모두 Free하는 함수
void free_tree(FileNode *root)
{
	if(root->childCnt <= 0)
		free(root);
	else {
		for(int i = 0; i < root->childCnt; i++)
			free_tree(root->child[i]);
		free(root->child);
		free(root);
	}
	return;
}

//before tree와 now tree를 비교하며 다른 파일에 대해 log 추가하는 함수
bool compare_tree(FileNode *before, FileNode* now)
{
	if(before == NULL)
		return false;
	int *beforeIdxMap = (int *)malloc(sizeof(int) * before->childCnt);	//각각의 child가 now의 몇번째 child와 동일한 이름인지 저장
	int *nowIdxMap = (int *)malloc(sizeof(int) * now->childCnt);	//각각의 child가 before의 몇번째 child와 동일한 이름인지 저장
	//IdxMap -1로 초기화
	for(int i = 0; i < before->childCnt; i++)
		beforeIdxMap[i] = -1;
	for(int i = 0; i < now->childCnt; i++)
		nowIdxMap[i] = -1;
	//IdxMap 완성
	for(int i = 0; i < before->childCnt; i++){
		for(int j = 0; j < now->childCnt; j++){
			if((before->child[i]->name)[0] == '.' && (before->child[i]->name)[1] == '/'){	//before 이름이 ./으로 시작할 경우 제거
				if(!strcmp((before->child[i]->name)+2, now->child[j]->name)){
					beforeIdxMap[i] = j;
					nowIdxMap[j] = i;
					char tmp[NAME_LEN];
					strcpy(tmp, (before->child[i]->name)+2);
					strcpy(before->child[i]->name, tmp);
				}
			}
			else if((now->child[j]->name)[0] == '.' && (now->child[j]->name)[1] == '/'){	//now 이름이 ./으로 시작할 경우 제거
				if(!strcmp(before->child[i]->name, (now->child[j]->name)+2)){
					beforeIdxMap[i] = j;
					nowIdxMap[j] = i;
					char tmp[NAME_LEN];
					strcpy(tmp, (now->child[j]->name)+2);
					strcpy(now->child[j]->name, tmp);
				}
			}
			else if(!strcmp(before->child[i]->name, now->child[j]->name)){
				beforeIdxMap[i] = j;
				nowIdxMap[j] = i;
			}
		}
	}
	//before child 탐색
	for(int i = 0; i < before->childCnt; i++){
		if(beforeIdxMap[i] == -1)	//now child에서 같은 파일명이 없을 경우
			add_log(DELETED, before->child[i]->name);	//DELETED log 추가
		else	//now child에서 같은 파일명을 가진 파일과 수정 시간이 다를 경우
			if(before->child[i]->mtime != now->child[beforeIdxMap[i]]->mtime)
				add_log(MODIFIED, before->child[i]->name);	//MODIFIED log 추가
	}
	//now child 탐색
	for(int i = 0; i < now->childCnt; i++)
		if(nowIdxMap[i] == -1)	//before child에서 같은 파일명이 없을 경우
			add_log(CREATED, now->child[i]->name);	//CREATED log 추가


	//before child 탐색하면서 directory이면서 now child에 같은 이름이 있을 경우 재귀 호출
	for(int i = 0; i < before->childCnt; i++){
		if(beforeIdxMap[i] != -1 && before->child[i]->childCnt > 0){
			compare_tree(before->child[i], now->child[beforeIdxMap[i]]);
			nowIdxMap[beforeIdxMap[i]] = -1;	//이후 반복문에서 같은 directory 다시 탐색하지 않도록 map에서 index 초기화
			beforeIdxMap[i] = -1;
		}
	}
	//now child 탐색하면서 directory이면서 before child에 같은 이름이 있을 경우 재귀 호출
	for(int i = 0; i < now->childCnt; i++){
		if(nowIdxMap[i] != -1 && now->child[i]->childCnt > 0){
			return compare_tree(before->child[nowIdxMap[i]], now->child[i]);
		}
	}
	free(beforeIdxMap);
	free(nowIdxMap);
	return true;
}
