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

using namespace qpid::messaging;
using namespace qpid::types;

using std::stringstream;
using std::string;

char buffer[1024*1024*5]={0};
int len=0;

pthread_t getcapcmd_id=0;
pthread_t sndimg_id=0;

sem_t req_img;
sem_t snd_img;

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
    wt_time.tv_nsec = 5*1000;//20 ms

    sem_init(&req_img,0,0);
    sem_init(&snd_img,0,0);

    pthread_create(&getcapcmd_id, NULL, getcapturecmd, NULL);
    pthread_create(&sndimg_id,NULL,sendimg,NULL);

    CvCapture* capture = cvCreateCameraCapture(0);
    
    IplImage* frame;
    while(1) 
    {
        double t = (double)cvGetTickCount();
        
        frame = cvQueryFrame(capture);
        if(frame == NULL)
            break;
        
        //*
        //process;
        if(sem_timedwait(&req_img,&wt_time)==0)
        {
            memcpy(buffer,frame->imageData,size640x480);
            //IplImage* pgray=cvCreateImage(cvGetSize(frame),IPL_DEPTH_8U,1);
            //cvCvtColor(frame,pgray,CV_BGR2GRAY);
            //memcpy(buffer,pgray->imageData,size640x480/3);
            //cvReleaseImage(&pgray);
            sem_post(&snd_img);
        }
        else
        {
           // printf("sem_timedwait error %d\n",errno);
        }
        //*/
        t = (double)cvGetTickCount() - t;
        printf( "exec time = %gms\n", t/(cvGetTickFrequency()*1000.));
    }
    cvReleaseCapture(&capture);

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