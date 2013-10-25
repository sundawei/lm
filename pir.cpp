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

char buffer[1024*1024*5]={0};
int len=0;

pthread_t getcapcmd_id=0;
pthread_t sndimg_id=0;
pthread_t globalcmd_id=0;

sem_t req_img;
sem_t snd_img;

#define MAX_CAR_COUNTING 100
int CA_Index=-1;
CvRect CA_A[MAX_CAR_COUNTING];
COLORREF CA_COLOR[MAX_CAR_COUNTING]={RGB(255,0,0)};
int CA_Type[MAX_CAR_COUNTING] = {0};//检测区类型:0常亮;1常灭;2闪烁


IplImage *imgprv=NULL;//前一帧彩色图像数据
IplImage *imggreyprv=NULL;//前一帧灰度数据

IplImage *colordiff=NULL;
IplImage *greydiff=NULL;
IplImage *testimg=NULL;


int RTmState[MAX_CAR_COUNTING][COUNT_V]={{0}};
int GetRectState(IplImage* img,CvRect rect)
{
	Mat m(img,0);
	int ct = countNonZero(m);
	if(ct>img->width*img->height*0.7)
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
	if(zerocount<=5)
		return 1;
	else if(onecount<=5)
		return 0;
	else
		return 2;
}

void makess(string& s)
{
	if(CA_Index>=0)
	{
		int len = 4*(CA_Index+1)+1;
		char tmp[5]={0};
		for(int  i=0;i<CA_Index;i++)
		{
			sprintf(tmp,"%d,%d;",GetRS(i),CA_Type[i]);
			s+=string(tmp);
		}
		sprintf(tmp,"%d,%d",GetRS(CA_Index),CA_Type[CA_Index]);
		s+=string(tmp);
	}
}

void sendStatus()
{
	const char* url = "amqp:*:9999";
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
        return 0;
    } catch(const std::exception& error) {
        std::cout << error.what() << std::endl;
        connection.close();
    }
    return 1;
}


void* getstatuscmd(void * param)
{
        const char* url =  "amqp:tcp:*:9999";
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
                                printf("[cmd]=%s\n",s.c_str());
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
					printf("no message, continue 2 wait\n");
					continue;
				} 
				decode(ms, content);
				string s=content["cmd"];
				printf("[cmd]=%s\n",s.c_str());
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

	sem_init(&req_img,0,0);
	sem_init(&snd_img,0,0);

	pthread_create(&getcapcmd_id, NULL, getcapturecmd, NULL);
	pthread_create(&sndimg_id,NULL,sendimg,NULL);
	pthread_create(&globalcmd_id,NULL,getstatuscmd,NULL);

	imgprv=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
	imggreyprv=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);

	colordiff=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,3);
	greydiff=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);
	testimg=cvCreateImage(cvSize(640,480),IPL_DEPTH_8U,1);

	int hist_size = 256;  
    float range[] = {0,255};
    float* ranges[]={range};  
    CvHistogram* gray_hist = cvCreateHist(1,&hist_size,CV_HIST_ARRAY,ranges,1);  

	CvCapture* capture = cvCreateCameraCapture(0);

	IplImage* frame;
	int framecount=0;
	while(1) 
	{
		double t = (double)cvGetTickCount();

		frame = cvQueryFrame(capture);
		if(frame == NULL)
			break;
		framecount++;

		IplImage* pgray=cvCreateImage(cvGetSize(frame),IPL_DEPTH_8U,1);



		cvCvtColor(frame,pgray,CV_BGR2GRAY);   
		if(framecount>=2)
		{
			// cvAbsDiff(frame,imgprv,colordiff);
			
			cvAbsDiff(pgray,imggreyprv,greydiff);
    		
    		cvCalcHist(&greydiff,gray_hist,0,0); 

    		int wcount=0;
    		int td=255;
    		for(int a=255;a>0;a--)
    		{
    			wcount+=(int)(*cvGetHistValue_1D(gray_hist,a));
    			//printf("%d\n",wcount);
    			if(wcount>640*480*0.1)
    			{
    				td=a;
    				break;
    			}
    		}

    		cvThreshold(greydiff, greydiff, (double)td, 255, CV_THRESH_BINARY);

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

		//process;
		if(sem_timedwait(&req_img,&wt_time)==0)
		{
			//memcpy(buffer,frame->imageData,size640x480);
			memcpy(buffer,colordiff->imageData,size640x480);
			sem_post(&snd_img);
		}
		else
		{   

		}

		memcpy(imgprv->imageData,frame->imageData,size640x480);//保存前一帧彩色数据
		memcpy(imggreyprv->imageData,pgray->imageData,size640x480/3);//保存前一帧灰度数据
		cvReleaseImage(&pgray);
		t = (double)cvGetTickCount() - t;
		printf( "exec time = %gms\n", t/(cvGetTickFrequency()*1000.));
	}
	cvReleaseCapture(&capture);

	cvReleaseImage(&imgprv);
	cvReleaseImage(&imggreyprv);
	cvReleaseImage(&colordiff);
	cvReleaseImage(&greydiff);

	return 1;
}
/*

const char* url = argc>1 ? argv[1] : "amqp:tcp:127.0.0.1:9999";
const char* address = argc>2 ? argv[2] : "message_queue; {create: always}";
std::string connectionOptions = argc > 3 ? argv[3] : "";

Connection connection(url, connectionOptions);
connection.setOption("reconnect", true);
try {
connection.open();
Session session = connection.createSession();
Sender sender = session.createSender(address);

Message message;
Variant::Map content;
content["id"] = 987654321;
content["name"] = "Widget";
content["percent"] = 0.99;
Variant::List colours;
colours.push_back(Variant("red"));
colours.push_back(Variant("green"));
colours.push_back(Variant("white"));
content["colours"] = colours;
content["uuid"] = Uuid(true);

CvCapture* capture = cvCreateCameraCapture(0);
IplImage* frame;
frame = cvQueryFrame(capture);
//  cvReleaseCapture(&capture);
//  content["iplimage"]=*frame;
string sip;
sip.assign((char*)frame,frame->nSize);
printf("capture size = %d\n",frame->nSize);     
content["iplimage"]=sip;
string sdd;
sdd.assign((char*)frame->imageData,frame->imageSize);
printf("imgsize = %d\n",frame->imageSize);
content["iplimagedata"]=sdd;
FILE* fp=fopen("./1.jpg","rb");
fseek(fp, 0, SEEK_END);
unsigned int size = ftell(fp);
fseek(fp, 0, SEEK_SET);
fread(buffer,size,1,fp);
fclose(fp);
string spic;
spic.assign(buffer,size);
content["pic"] = spic;


printf("%s\n",content["uuid"].asString().c_str());
encode(content, message);

sender.send(message, true);

connection.close();
cvReleaseCapture(&capture);
return 0;
} catch(const std::exception& error) {
std::cout << error.what() << std::endl;
connection.close();
}
int ip=0;
std::cin>>ip;
*/
