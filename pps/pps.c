#include "header.h"

#define COLUMN_CNT 12	//출력할 column 최대 갯수

//출력 시 사용할 columnWidth배열에서의 index
#define USER_IDX 0
#define PID_IDX 1
#define CPU_IDX 2
#define MEM_IDX 3
#define VSZ_IDX 4
#define RSS_IDX 5
#define TTY_IDX 6
#define STAT_IDX 7
#define START_IDX 8
#define TIME_IDX 9
#define CMD_IDX 10
#define COMMAND_IDX 11

myProc procList[PROCESS_MAX];	//완성한 myProc의 포인터 저장 배열

int procCnt = 0;				//현재까지 완성한 myProc 갯수

unsigned long uptime;			//os 부팅 후 지난 시간
unsigned long beforeUptime;		//이전 실행 시의 os 부팅 후 지난 시각
unsigned long memTotal;			//전체 물리 메모리 크기
unsigned int hertz;	 			//os의 hertz값 얻기(초당 context switching 횟수)

time_t now;
time_t before;

pid_t myPid;					//자기 자신의 pid
uid_t myUid;					//자기 자신의 uid
char myPath[PATH_LEN];			//자기 자신의 path
char myTTY[TTY_LEN];			//자기 자신의 tty

bool aOption = false;			//a Option	stat, command 활성화
bool uOption = false;			//u Option	user, cpu, mem, vsz, rss, start, command 활성화
bool xOption = false;			//x Option	stat, command 활성화

int termWidth;					//현재 터미널 너비

//실제 화면에 출력하는 함수
void print_pps(void)
{

	int columnWidth[COLUMN_CNT] = {					//column의 x축 길이 저장하는 배열
		strlen(USER_STR), strlen(PID_STR), strlen(CPU_STR), strlen(MEM_STR),
		strlen(VSZ_STR), strlen(RSS_STR), strlen(TTY_STR), strlen(STAT_STR),
		strlen(START_STR), strlen(TIME_STR), strlen(CMD_STR), strlen(COMMAND_STR) };
	
	char buf[BUFFER_SIZE];

	for(int i = 0; i < procCnt; i++)			//USER 최대 길이 저장
		if(columnWidth[USER_IDX] < strlen(procList[i].user)){
			columnWidth[USER_IDX] = strlen(procList[i].user);
		}

	for(int i = 0; i < procCnt; i++){			//PID 최대 길이 저장
		sprintf(buf, "%lu", procList[i].pid);
		if(columnWidth[PID_IDX] < strlen(buf))
			columnWidth[PID_IDX] = strlen(buf);
	}

	for(int i = 0; i < procCnt; i++){			//CPU 최대 길이 저장
		sprintf(buf, "%3.1Lf", procList[i].cpu);
		if(columnWidth[CPU_IDX] < strlen(buf))
			columnWidth[CPU_IDX] = strlen(buf);
	}

	for(int i = 0; i < procCnt; i++){			//MEM 최대 길이 저장
		sprintf(buf, "%3.1Lf", procList[i].mem);
		if(columnWidth[MEM_IDX] < strlen(buf))
			columnWidth[MEM_IDX] = strlen(buf);
	}

	for(int i = 0; i < procCnt; i++){			//VSZ 최대 길이 저장
		sprintf(buf, "%lu", procList[i].vsz);
		if(columnWidth[VSZ_IDX] < strlen(buf))
			columnWidth[VSZ_IDX] = strlen(buf);
	}

	for(int i = 0; i < procCnt; i++){			//RSS 최대 길이 저장
		sprintf(buf, "%lu", procList[i].rss);
		if(columnWidth[RSS_IDX] < strlen(buf))
			columnWidth[RSS_IDX] = strlen(buf);
	}

	for(int i = 0; i < procCnt; i++){			//TTY 최대 길이 저장
		if(columnWidth[TTY_IDX] < strlen(procList[i].tty))
			columnWidth[TTY_IDX] = strlen(procList[i].tty);
	}

	for(int i = 0; i < procCnt; i++){			//STAT 최대 길이 저장
		if(columnWidth[STAT_IDX] < strlen(procList[i].stat))
			columnWidth[STAT_IDX] = strlen(procList[i].stat);
	}

	for(int i = 0; i < procCnt; i++){			//START 최대 길이 저장
		if(columnWidth[START_IDX] < strlen(procList[i].start))
			columnWidth[START_IDX] = strlen(procList[i].start);
	}

	for(int i = 0; i < procCnt; i++){			//TIME 최대 길이 저장
		if(columnWidth[TIME_IDX] < strlen(procList[i].time))
			columnWidth[TIME_IDX] = strlen(procList[i].time);
	}

	for(int i = 0; i < procCnt; i++){			//CMD 최대 길이 저장
		if(columnWidth[CMD_IDX] < strlen(procList[i].cmd))
			columnWidth[CMD_IDX] = strlen(procList[i].cmd);
	}

	for(int i = 0; i < procCnt; i++){			//COMMAND 최대 길이 저장
		if(columnWidth[COMMAND_IDX] < strlen(procList[i].command))
			columnWidth[COMMAND_IDX] = strlen(procList[i].command);
	}

	//columnWidth 완성
	if(!aOption && !uOption && !xOption){		// pps 인 경우
		columnWidth[USER_IDX] = -1;	//USER 출력 X
		columnWidth[CPU_IDX] = -1;	//CPU 출력 X
		columnWidth[MEM_IDX] = -1;	//MEM 출력 X
		columnWidth[VSZ_IDX] = -1;	//VSZ 출력 X
		columnWidth[RSS_IDX] = -1;	//RSS 출력 X
		columnWidth[STAT_IDX] = -1;	//STAT 출력 X
		columnWidth[START_IDX] = -1;//START 출력 X
		columnWidth[COMMAND_IDX] = -1;//COMMAND 출력 X
	}
	else if(!uOption){							// pps a / pps x / pps ax인 경우
		columnWidth[USER_IDX] = -1;	//USER 출력 X
		columnWidth[CPU_IDX] = -1;	//CPU 출력 X
		columnWidth[MEM_IDX] = -1;	//MEM 출력 X
		columnWidth[VSZ_IDX] = -1;	//VSZ 출력 X
		columnWidth[RSS_IDX] = -1;	//RSS 출력 X
		columnWidth[START_IDX] = -1;//START 출력 X
		columnWidth[CMD_IDX] = -1;	//CMD 출력 X
	}
	else										// pps u / pps au / pps ux / pps aux인 경우
		columnWidth[CMD_IDX] = -1;	//CMD 출력 X
	
	/*****		column 출력 시작	*****/
	int gap = 0;

	memset(buf, '\0', BUFFER_SIZE);
	//USER 출력
	if(uOption){
		gap = columnWidth[USER_IDX] - strlen(USER_STR);	//USER의 길이 차 구함

		strcat(buf, USER_STR);
		for(int i = 0; i < gap; i++)				//USER 좌측 정렬
			strcat(buf, " ");

		strcat(buf, " ");
	}

	//PID 출력
	gap = columnWidth[PID_IDX] - strlen(PID_STR);	//PID의 길이 차 구함
	for(int i = 0; i < gap; i++)				//PID 우측 정렬
		strcat(buf, " ");
	strcat(buf, PID_STR);

	strcat(buf, " ");

	//%CPU 출력
	if(uOption){
		gap = columnWidth[CPU_IDX] - strlen(CPU_STR);	//CPU의 길이 차 구함
		for(int i = 0; i < gap; i++)				//CPU 우측 정렬
			strcat(buf, " ");
		strcat(buf, CPU_STR);

		strcat(buf, " ");
	}

	//%MEM 출력
	if(uOption){
		gap = columnWidth[MEM_IDX] - strlen(MEM_STR);	//MEM의 길이 차 구함
		for(int i = 0; i < gap; i++)				//MEM 우측 정렬
			strcat(buf, " ");
		strcat(buf, MEM_STR);

		strcat(buf, " ");
	}

	//VSZ 출력
	if(uOption){
		gap = columnWidth[VSZ_IDX] - strlen(VSZ_STR);	//VSZ의 길이 차 구함
		for(int i = 0; i < gap; i++)				//VSZ 우측 정렬
			strcat(buf, " ");
		strcat(buf, VSZ_STR);

		strcat(buf, " ");
	}

	//RSS 출력
	if(uOption){
		gap = columnWidth[RSS_IDX] - strlen(RSS_STR);	//RSS의 길이 차 구함
		for(int i = 0; i < gap; i++)				//RSS 우측 정렬
			strcat(buf, " ");
		strcat(buf, RSS_STR);

		strcat(buf, " ");
	}

	//TTY 출력
	gap = columnWidth[TTY_IDX] - strlen(TTY_STR);	//TTY의 길이 차 구함
	strcat(buf, TTY_STR);
	for(int i = 0; i < gap; i++)				//TTY 좌측 정렬
		strcat(buf, " ");

	strcat(buf, " ");

	//STAT 출력
	if(aOption || uOption || xOption){
		gap = columnWidth[STAT_IDX] - strlen(STAT_STR);	//STAT의 길이 차 구함
		strcat(buf, STAT_STR);
		for(int i = 0; i < gap; i++)				//STAT 좌측 정렬
			strcat(buf, " ");

		strcat(buf, " ");
	}

	//START 출력
	if(uOption){
		gap = columnWidth[START_IDX] - strlen(START_STR);//START의 길이 차 구함
		strcat(buf, START_STR);
		for(int i = 0; i < gap; i++)				//START 좌측 정렬
			strcat(buf, " ");

		strcat(buf, " ");
	}

	//TIME 출력
	gap = columnWidth[TIME_IDX] - strlen(TIME_STR);	//TIME의 길이 차 구함
	for(int i = 0; i < gap; i++)				//TIME 우측 정렬
		strcat(buf, " ");
	strcat(buf, TIME_STR);

	strcat(buf, " ");

	//COMMAND 또는 CMD 출력
	if(aOption || uOption || xOption)
		strcat(buf, COMMAND_STR);
	else
		strcat(buf, CMD_STR);					//CMD 바로 출력

	buf[COLS] = '\0';							//터미널 너비만큼 잘라 출력
	printf("%s\n", buf);

	/*****		column 출력 종료	*****/


	/*****		process 출력 시작	*****/

	char token[TOKEN_LEN];
	memset(token, '\0', TOKEN_LEN);

	for(int i = 0; i < procCnt; i++){
		memset(buf, '\0', BUFFER_SIZE);

		//USER 출력
		if(uOption){
			gap = columnWidth[USER_IDX] - strlen(procList[i].user);	//USER의 길이 차 구함
			strcat(buf, procList[i].user);
			for(int i = 0; i < gap; i++)				//USER 좌측 정렬
				strcat(buf, " ");

			strcat(buf, " ");
		}

		//PID 출력
		memset(token, '\0', TOKEN_LEN);
		sprintf(token, "%lu", procList[i].pid);
		gap = columnWidth[PID_IDX] - strlen(token);		//PID의 길이 차 구함
		for(int i = 0; i < gap; i++)				//PID 우측 정렬
			strcat(buf, " ");
		strcat(buf, token);

		strcat(buf, " ");

		//%CPU 출력
		if(uOption){
			memset(token, '\0', TOKEN_LEN);
			sprintf(token, "%3.1Lf", procList[i].cpu);
			gap = columnWidth[CPU_IDX] - strlen(token);	//CPU의 길이 차 구함
			for(int i = 0; i < gap; i++)			//CPU 우측 정렬
				strcat(buf, " ");
			strcat(buf, token);

			strcat(buf, " ");
		}

		//%MEM 출력
		if(uOption){
			memset(token, '\0', TOKEN_LEN);
			sprintf(token, "%3.1Lf", procList[i].mem);
			gap = columnWidth[MEM_IDX] - strlen(token);	//MEM의 길이 차 구함
			for(int i = 0; i < gap; i++)			//MEM 우측 정렬
				strcat(buf, " ");
			strcat(buf, token);

			strcat(buf, " ");
		}

		//VSZ 출력
		if(uOption){
			memset(token, '\0', TOKEN_LEN);
			sprintf(token, "%lu", procList[i].vsz);
			gap = columnWidth[VSZ_IDX] - strlen(token);	//VSZ의 길이 차 구함
			for(int i = 0; i < gap; i++)			//VSZ 우측 정렬
				strcat(buf, " ");
			strcat(buf, token);

			strcat(buf, " ");
		}

		//RSS 출력
		if(uOption){
			memset(token, '\0', TOKEN_LEN);
			sprintf(token, "%lu", procList[i].rss);
			gap = columnWidth[RSS_IDX] - strlen(token);	//RSS의 길이 차 구함
			for(int i = 0; i < gap; i++)			//RSS 우측 정렬
				strcat(buf, " ");
			strcat(buf, token);

			strcat(buf, " ");
		}

		//TTY 출력
		gap = columnWidth[TTY_IDX] - strlen(procList[i].tty);	//TTY의 길이 차 구함
		strcat(buf, procList[i].tty);
		for(int i = 0; i < gap; i++)						//TTY 좌측 정렬
			strcat(buf, " ");

		strcat(buf, " ");

		//STAT 출력
		if(aOption || uOption || xOption){
			gap = columnWidth[STAT_IDX] - strlen(procList[i].stat);	//STAT의 길이 차 구함
			strcat(buf, procList[i].stat);
			for(int i = 0; i < gap; i++)				//STAT 좌측 정렬
				strcat(buf, " ");

			strcat(buf, " ");
		}

		//START 출력
		if(uOption){
			gap = columnWidth[START_IDX] - strlen(procList[i].start);	//START의 길이 차 구함
			strcat(buf, procList[i].start);
			for(int i = 0; i < gap; i++)				//START 좌측 정렬
				strcat(buf, " ");

			strcat(buf, " ");
		}

		//TIME 출력
		gap = columnWidth[TIME_IDX] - strlen(procList[i].time);	//TIME의 길이 차 구함
		for(int i = 0; i < gap; i++)				//TIME 우측 정렬
			strcat(buf, " ");
		strcat(buf, procList[i].time);

		strcat(buf, " ");

		//COMMAND 또는 CMD 출력
		if(aOption || uOption || xOption){
			strcat(buf, procList[i].command);		//COMMAND 바로 출력
		}
		else{
			strcat(buf, procList[i].cmd);			//CMD 바로 출력
		}

		buf[COLS] = '\0';							//터미널 너비만큼만 출력
		printf("%s\n", buf);

	}

	/*****		process 출력 종료	*****/

	return;
}

int main(int argc, char *argv[])
{
	memTotal = get_mem_total();					//전체 물리 메모리 크기
	hertz = (unsigned int)sysconf(_SC_CLK_TCK);	//os의 hertz값 얻기(초당 context switching 횟수)

	myPid = getpid();			//자기 자신의 pid

	char pidPath[FNAME_LEN];
	memset(pidPath, '\0', FNAME_LEN);
	sprintf(pidPath, "/%d", myPid);

	strcpy(myPath, PROC);			//자기 자신의 /proc 경로 획득
	strcat(myPath, pidPath);

	getTTY(myPath, myTTY);			//자기 자신의 tty 획득
	for(int i = strlen(PTS); i < strlen(myTTY); i++)
		if(!isdigit(myTTY[i])){
			myTTY[i] = '\0';
			break;
		}

	myUid = getuid();			//자기 자신의 uid

	initscr();				//출력 윈도우 초기화
	termWidth = COLS;		//term 너비 획득
	endwin();				//출력 윈도우 종료

	for(int i = 1; i < argc; i++){					//Option 획득
		for(int j = 0; j < strlen(argv[i]); j++){
			switch(argv[i][j]){
				case 'a':
					aOption = true;
					break;
				case 'u':
					uOption = true;
					break;
				case 'x':
					xOption = true;
					break;
			}
		}
	}

	search_proc(true, aOption, uOption, xOption, NULL);

	print_pps();

	return 0;
}
