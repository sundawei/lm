#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <opencv/cv.h>
#include <opencv2/opencv.hpp>
#include <opencv/cxcore.h>
#include <opencv/highgui.h>
#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Message.h>
#include <qpid/messaging/Sender.h>
#include <qpid/messaging/Session.h>
#include <qpid/messaging/Receiver.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <semaphore.h>
#include <errno.h>

#define size640x480 640*480*3
typedef unsigned long DWORD;
typedef DWORD   COLORREF;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
#define RGB(r,g,b)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
using namespace qpid::messaging;
using namespace qpid::types;
using std::stringstream;
using std::string;
using namespace cv;
char buffer[1024*1024*5]={0};
int len=0;
pthread_t getcapcmd_id=0;
pthread_t sndimg_id=0;
pthread_t globalcmd_id=0;
sem_t req_img;
sem_t snd_img;
#define MAX_CAR_COUNTING 100
#define COUNT_V 20
int CA_Index=-1;
CvRect CA_A[MAX_CAR_COUNTING];
COLORREF CA_COLOR[MAX_CAR_COUNTING]={RGB(255,0,0)};
int CA_Type[MAX_CAR_COUNTING] = {0};//检测区类型:0常亮;1常灭;2闪烁
char CA_Name[MAX_CAR_COUNTING][32] = {{0}};
int RTmState[MAX_CAR_COUNTING][COUNT_V]={{0}};

int CA_countBmin[MAX_CAR_COUNTING]={0};//每个检测区检测到的最小亮度
int CA_countBmax[MAX_CAR_COUNTING]={0};//每个检测区检测到的最大亮度
int CA_state[MAX_CAR_COUNTING]={-1};//每隔一段时间跟新每个检测区的状态

#define COUNT_SPAN 60//每隔60帧统计一次
#define LIGHTS 1.9//统计区最亮平均值比最暗平均值如果大于1.9倍，则认为是闪烁状态


IplImage *imgprv=NULL;//前一帧彩色图像数据
IplImage *imggreyprv=NULL;//前一帧灰度数据
IplImage *imghsv=NULL;//转化到HSV色彩空间
IplImage *colordiff=NULL;
IplImage *greydiff=NULL;
IplImage *testimg=NULL;

int prvCntFrameCount=0;
float averagelight=0.0;//图像的平均亮度
float lightvalue=0.0f;//如果图像上发现了亮的区域则统计亮值的平均值记录下来，用来区分画面上其他的灯亮灯灭的区域


void LoadConfigBuffer(std::string & s)
{
	int len = s.size();
	char* pb =  new char[len];
	memcpy(pb,s.data(),len);
	memcpy(CA_A,pb,sizeof(CA_A));
	memcpy(CA_COLOR,pb+sizeof(CA_A),sizeof(CA_COLOR));
	memcpy(&CA_Index,pb+sizeof(CA_A)+sizeof(CA_COLOR),sizeof(CA_Index));
	memcpy(CA_Type,pb+sizeof(CA_A)+sizeof(CA_COLOR)+sizeof(CA_Index),sizeof(CA_Type));
	memcpy(CA_Name,pb+sizeof(CA_A)+sizeof(CA_COLOR)+sizeof(CA_Index)+sizeof(CA_Type),sizeof(CA_Name));
	delete [] pb;
}

int GetAreaSumBright(IplImage* src,CvRect r)
{
	IplImage* imgr=0;
	int sum=0;
	unsigned char* pstart=(unsigned char*)src->imageData;
	for(int i=r.y;i<r.y+r.height;i++)
	{
		for(int j=r.x;j<r.x+r.width;j++)
		{
			sum+=*(pstart+i*src->width*3+j*3+2);
		}
	}
	return sum/(r.width*r.height);
}

int GetRectState(IplImage* img,CvRect rect)
{
	Mat m(img,0);
	int ct = countNonZero(m);
	if(ct>img->width*img->height*0.2)
		return 0;//on
	else
		return 1;//off
}
void CountAllR(IplImage* img,int framect)
{
	if(CA_Index>=0)
	{
		for(int i=0;i<=CA_Index;i++)
		{
			RTmState[i][framect%COUNT_V]=GetRectState(img,CA_A[i]);
		}
	}
}
int GetRS(int id)
{
	int zerocount=0;
	int onecount=0;
	for(int i=0;i<COUNT_V;i++)
	{
		if(RTmState[id][i]==0)
			zerocount++;
		else
			onecount++;
	}
	if(zerocount<=2)
		return 1;
	else if(onecount<=2)
		return 0;
	else
		return 2;
}
void makess(string& s)
{
	/*
	if(CA_Index>=0)
	{
		char tmp[5]={0};
		for(int  i=0;i<CA_Index;i++)
		{
			sprintf(tmp,"%d,%d;",GetRS(i),CA_Type[i]);
			s+=string(tmp);
		}
		sprintf(tmp,"%d,%d",GetRS(CA_Index),CA_Type[CA_Index]);
		s+=string(tmp);
	}
	//*/

	if(CA_Index>=0)
	{
		char tmp[5]={0};
		int i=0;
		for(i=0;i<CA_Index;i++)
		{
			sprintf(tmp,"%d,%d;",CA_state[i],CA_Type[i]);
			s+=string(tmp);
		}
		sprintf(tmp,"%d,%d",CA_state[i],CA_Type[CA_Index]);
		s+=string(tmp);
	}
}
void sendStatus()
{
	const char* url = "amqp:127.0.0.1:9999";//global server
    const char* address = "message_queue_status_place0; {create: always}";
    std::string connectionOptions =  "";
    
    Connection connection(url, connectionOptions);
	connection.setOption("reconnect", true);
    try {
        connection.open();
        Session session = connection.createSession();
        Sender sender = session.createSender(address);
        Message message;
        Variant::Map content;
		content["idx"] = CA_Index;
		string s;
		makess(s);
		content["status"] = s;
        encode(content, message);
        sender.send(message, true);
        connection.close();
    } catch(const std::exception& error) {
        std::cout << error.what() << std::endl;
        connection.close();
    }
}
void* getstatuscmd(void * param)
{
        const char* url =  "amqp:tcp:127.0.0.1:9999";//global server
        const char* address = "globalcmd; {create: always}";
        std::string connectionOptions ="";
        while(1)
        {
                Connection connection(url,connectionOptions);
                connection.setOption("reconnect", true);
                connection.open();
                Session session = connection.createSession();
                Receiver receiver = session.createReceiver(address);
                while(1)
                {
                        try {
                                Variant::Map content;
                                Message ms;
                                if(!receiver.fetch(ms,Duration::SECOND*10))
                                {
                                        if(receiver.isClosed())
                                                break;
                                        printf("no global message, continue 2 wait\n");
                                        continue;
                                } 
                                decode(ms, content);
                                string s=content["cmd"];
                                printf("[globalcmd]=%s\n",s.c_str());
                                if(s=="s")
                                {
                                        sendStatus();
                                }
                                session.acknowledge();
                        } 
                        catch(const std::exception& error) {
                                receiver.close();
                        }
                }
                connection.close();
        }   
        return 0;
}
void sendconfig(string s)//send config file content to computer
{
	const char* url = "amqp:tcp:127.0.0.1:9999";
	const char* address = "conf; {create: always}";
	std::string connectionOptions = "";

	Connection connection(url, connectionOptions);
	connection.setOption("reconnect", true);
	connection.open();
	Session session = connection.createSession();
	Sender sender = session.createSender(address);
	try {
		Variant::Map content;
		Message ms;
		string spic;
		content["pic"] = s;
		content["len"] = s.size();
		encode(content, ms);
		sender.send(ms, true);
	} 
	catch(const std::exception& error) 
	{
		sender.close();
	}
	connection.close();
	return ;
}
void* getcapturecmd(void * param)
{
	const char* url =  "amqp:tcp:127.0.0.1:9999";
	const char* address = "cmd; {create: always}";
	std::string connectionOptions ="";
	while(1)
	{
		Connection connection(url,connectionOptions);
		connection.setOption("reconnect", true);
		connection.open();
		Session session = connection.createSession();
		Receiver receiver = session.createReceiver(address);
		while(1)
		{
			try {
				Variant::Map content;
				Message ms;
				if(!receiver.fetch(ms,Duration::SECOND*10))
				{
					if(receiver.isClosed())
						break;
					//printf("no message, continue 2 wait\n");
					continue;
				} 
				decode(ms, content);
				string s=content["cmd"];
				//printf("[cmd]=%s\n",s.c_str());
				if(s=="cap")
				{
					sem_post(&req_img);
				}
				if(s=="rcn")//got config file from computer
				{
					string sconfig=content["configfile"];
					FILE *fp = fopen("./config.area","wb");
					fwrite(sconfig.data(),sconfig.size(),1,fp);
					fclose(fp);
					LoadConfigBuffer(sconfig);
				}
				if(s=="gcn")//computer want 2 get config file from raspberry pi
				{
					FILE* fp=fopen("./config.area","rb");
					if(fp!=NULL)
					{
						fseek(fp, 0, SEEK_END);
						unsigned int size = ftell(fp);
						fseek(fp, 0, SEEK_SET);
						char* buffer =new char[size];
						fread(buffer,size,1,fp);
						fclose(fp);
						string sd;
						sd.assign(buffer,size);
						delete [] buffer;
						sendconfig(sd);
					}
					else//no config file
					{
						sendconfig(std::string(""));
					}
				}
				session.acknowledge();
			} 
			catch(const std::exception& error) {
				receiver.close();
			}
		}
		connection.close();
	}   
	return 0;
}
void* sendimg(void * param)//send captured img
{
	const char* url = "amqp:tcp:127.0.0.1:9999";
	const char* address = "img; {create: always}";
	std::string connectionOptions = "";
	while(1)
	{
		Connection connection(url, connectionOptions);
		connection.setOption("reconnect", true);
		connection.open();
		Session session = connection.createSession();
		Sender sender = session.createSender(address);
		while(1)
		{
			try {
				if(sem_wait(&snd_img)==0)//get a cmd 2 snd img
				{
					Variant::Map content;
					Message ms;
					string spic;
					spic.assign(buffer,size640x480);
					content["pic"] = spic;
					encode(content, ms);
					sender.send(ms, true);
				}
			} 
			catch(const std::exception& error) 
			{
				sender.close();
			}
		}
		connection.close();
	}
	return 0;
}

int main(int argc, char** argv) {
	struct timespec wt_time;
	wt_time.tv_sec = 0;
	wt_time.tv_nsec = 1*1000;//20 ms
	FILE* fp=fopen("./config.area","rb");
	if(fp!=NULL)
	{
		fseek(fp, 0, SEEK_END);
		unsigned int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* buffer =new char[size];
		fread(buffer,size,1,fp);
		fclose(fp);
		string sd;
		sd.assign(buffer,size);
		delete [] buffer;
		LoadConfigBuffer(sd);
	}
	


	sem_init(&req_img,0,0);
	sem_init(&snd_img,0,0);
	pthread_create(&getcapcmd_id, NULL, getcapturecmd, NULL);
	pthread_create(&sndimg_id,NULL,sendimg,NULL);
	pthread_create(&globalcmd_id,NULL,getstatuscmd,NULL);
	imgprv=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
	imghsv=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
	imggreyprv=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
	colordiff=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
	greydiff=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
	testimg=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
	int hist_size = 256;  
    float range[] = {0,255};
    float* ranges[]={range};  
    CvHistogram* gray_hist = cvCreateHist(1,&hist_size,CV_HIST_ARRAY,ranges,1);  
#ifdef USECAM    
	CvCapture* capture = cvCreateCameraCapture(0);
#else
	CvCapture* capture = cvCreateFileCapture("./pir.mp4");	
#endif	
	IplImage* frame;
	int framecount=0;
	while(1) 
	{
		double t = (double)cvGetTickCount();
		frame = cvQueryFrame(capture);
		if(frame == NULL)
#ifdef USECAM			
		{
			break;
		}
#else
		{
			cvReleaseCapture(&capture);
			capture = cvCreateFileCapture("./pir.mp4");	
			frame = cvQueryFrame(capture);
			if(frame == NULL)
			{
				break;
			}
		}	
#endif

		framecount++;
		cvCvtColor(frame,imghsv,CV_BGR2HSV);
		for(int i=0;i<=CA_Index;i++)
		{
			int ret=GetAreaSumBright(imghsv,CA_A[i]);
			//printf("%d,%d\n",i,ret);
			if(ret>CA_countBmax[i])
			{
				CA_countBmax[i]=ret;
			}
			if(ret<CA_countBmin[i])
			{
				CA_countBmin[i]=ret;
			}
		}
		if(framecount%10==0)//每隔3帧统计一下平均亮度
		{
			 CvScalar Scalar1;
    		 Scalar1 = cvAvg(imghsv);
    		 averagelight = Scalar1.val[2];
		}

		if(framecount-prvCntFrameCount>=COUNT_SPAN)//60帧统计一次灯的状态
		{
			prvCntFrameCount=framecount;



			for(int i=0;i<=CA_Index;i++)//重新统计
			{

				CA_countBmin[i]>0?CA_countBmin[i]:CA_countBmin[i]=1;

				if((float)CA_countBmax[i]/(float)CA_countBmin[i]>LIGHTS)
				{
					CA_state[i]=2;
					if(lightvalue==0.0f)
					lightvalue=CA_countBmax[i];
					else
					{
						lightvalue=(float)(lightvalue+CA_countBmax[i])/2.0f;
					}
				}
				else
				{
					if(lightvalue!=0.0f)//画面上曾经确定了灯亮的区域
					{
						if(CA_countBmax[i]>=lightvalue*0.8)
						{
							CA_state[i]=0;//常亮
						}
						else
						{
							CA_state[i]=1;//常灭
						}
					}
					else
					{
						if(CA_countBmax[i]>averagelight*1.2)//亮度值大于平均亮度值%20，认为灯是常亮的
						{
							CA_state[i]=0;//常亮
						}
						else
						{
							CA_state[i]=1;//长灭
						}
					}
					//CA_state[i]=0;
				}
				CA_countBmin[i]=1000;//每个检测区检测到的最小亮度
				CA_countBmax[i]=0;//每个检测区检测到的最大亮度
				//int CA_state[MAX_CAR_COUNTING]={0};//每隔一段时间跟新每个检测区的状态
			}
		}

		for(int i=0;i<=CA_Index;i++)
		{
			if(CA_state[i]==2)
			{
				printf("%d,blink\n",i);
			}
			else if(CA_state[i]==0)
			{
				printf("%d,on\n",i);
			}
			else if(CA_state[i]==1)
			{
				printf("%d,off\n",i);
			}
			else
			{
				printf("%d,unknown\n",i);
			}
		}

		/*
		IplImage* pgray=cvCreateImage(cvGetSize(frame),IPL_DEPTH_8U,1);
		cvCvtColor(frame,pgray,CV_BGR2GRAY);   
		if(framecount>=2)
		{	
			cvAbsDiff(pgray,imggreyprv,greydiff);	
    		cvCalcHist(&greydiff,gray_hist,0,0); 
    		int wcount=0;
    		int td=255;
    		for(int a=255;a>0;a--)
    		{
    			wcount+=(int)(*cvGetHistValue_1D(gray_hist,a));
    			if(wcount>640*480*0.1)
    			{
    				td=a;
    				break;
    			}
    		}
    		//if(td<100)
    		//	td=100;
    		//td=60;
    		cvThreshold(greydiff, greydiff, (double)30, 255, CV_THRESH_BINARY);
    		CountAllR(greydiff,framecount);
			for(int i=0;i<colordiff->height;i++)
			{
				for(int j=0;j<colordiff->width;j++)
				{
					int A=CV_IMAGE_ELEM(greydiff,unsigned char,i,j);
					if(A>td)
					{   
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+0)=255;//CV_IMAGE_ELEM(pgray,unsigned char,i,j);
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+1)=255;//CV_IMAGE_ELEM(pgray,unsigned char,i,j);
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+2)=255;//CV_IMAGE_ELEM(pgray,unsigned char,i,j);
					}
					else
					{
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+0)=0;
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+1)=0;
						CV_IMAGE_ELEM(colordiff,unsigned char,i,j*3+2)=0;
					}
				}
			}
		}
		//*/
		//process;
		if(sem_timedwait(&req_img,&wt_time)==0)
		{
			memcpy(buffer,frame->imageData,size640x480);
			sem_post(&snd_img);
		}
		else
		{   
			;
		}
		//memcpy(imgprv->imageData,frame->imageData,size640x480);//保存前一帧彩色数据
		//memcpy(imggreyprv->imageData,pgray->imageData,size640x480/3);//保存前一帧灰度数据
		//cvReleaseImage(&pgray);
		t = (double)cvGetTickCount() - t;
		//printf( "exec time = %gms\n", t/(cvGetTickFrequency()*1000.));
	}
	cvReleaseCapture(&capture);
	cvReleaseImage(&imgprv);
	cvReleaseImage(&imghsv);
	cvReleaseImage(&imggreyprv);
	cvReleaseImage(&colordiff);
	cvReleaseImage(&greydiff);
	return 1;
}
