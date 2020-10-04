#include "header.h"

//time_t를 customTime 구조체로 변경하는 함수
void time_to_customTime(time_t t_time, customTime *ct)
{
	struct tm *t;
	t = localtime(&t_time);
	ct->year = t->tm_year + 1900;
	ct->month = t->tm_mon + 1;
	ct->day = t->tm_mday;
	ct->hour = t->tm_hour;
	ct->min = t->tm_min;
	ct->sec = t->tm_sec;
	return;
}

//customTime 구조체를 문자열로 변경하는 함수
void customTime_to_str(customTime *ct, char *str)
{
	sprintf(str, "%04d-%02d-%02d %02d:%02d:%02d", ct->year, ct->month, ct->day, ct->hour, ct->min, ct->sec);
	return;
}

//문자열을 customTime 구조체로 변경하는 함수
bool str_to_customTime(char *str, customTime *ct)
{
	if(strlen(str) > 16)
		sscanf(str, "%04d-%02d-%02d %02d:%02d:%02d", &(ct->year), &(ct->month), &(ct->day), &(ct->hour), &(ct->min), &(ct->sec));
	else{
		sscanf(str, "%04d-%02d-%02d %02d:%02d", &(ct->year), &(ct->month), &(ct->day), &(ct->hour), &(ct->min));
		ct->sec = 0;
	}
	if((ct->year < 0) || (ct->year > 9999))
		return false;
	else if((ct->month < 1) || (ct->month > 12))
		return false;
	else if((ct->day < 1) || (ct->day > 31))
		return false;
	else if((ct->hour < 0) || (ct->hour > 23))
		return false;
	else if((ct->min < 0) || (ct->min > 59))
		return false;
	else if((ct->sec < 0) || (ct->sec > 59))
		return false;
	return true;
}

//customTime을 복사하는 함수
void copy_customTime(customTime *dst, customTime *src)
{
	dst->year = src->year;
	dst->month = src->month;
	dst->day = src->day;
	dst->hour = src->hour;
	dst->min = src->min;
	dst->sec = src->sec;
	return;
}

//ct1이 ct2보다 이전이거나 같은 시간인지 비교하는 함수
bool is_before(customTime *ct1, customTime *ct2)
{
	if(ct1->year < ct2->year)
		return true;
	else if(ct1->year > ct2->year)
		return false;
	else {
		if(ct1->month < ct2->month)
			return true;
		else if(ct1->month > ct2->month)
			return false;
		else {
			if(ct1->day < ct2->day)
				return true;
			else if(ct1->day > ct2->day)
				return false;
			else {
				if(ct1->hour < ct2->hour)
					return true;
				else if(ct1->hour > ct2->hour)
					return false;
				else {
					if(ct1->min < ct2->min)
						return true;
					else if(ct1->min > ct2->min)
						return false;
					else {
						if(ct1->sec < ct2->sec)
							return true;
						else if(ct1->sec > ct2->sec)
							return false;
						else
							return true;
					}
				}
			}
		}
	}
}

//customTime이 현재 시간보다 이전이거나 같은 시간인지 확인하는 함수
bool is_before_than_now(customTime *ct)
{
	time_t now;
	now = time(NULL);
	customTime nowCt;
	time_to_customTime(now, &nowCt);
	return is_before(ct, &nowCt);
}
