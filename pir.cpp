#include <stdio.h>
#include <opencv/highgui.h>
#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Message.h>
#include <qpid/messaging/Sender.h>
#include <qpid/messaging/Session.h>

#include <cstdlib>
#include <iostream>

#include <sstream>
#include <string>


using namespace qpid::messaging;
using namespace qpid::types;

using std::stringstream;
using std::string;

char buffer[1024*1024*5]={0};
int len=0;


int main(int argc, char** argv) {
    const char* url = argc>1 ? argv[1] : "amqp:tcp:127.0.0.1:9999";
    const char* address = argc>2 ? argv[2] : "message_queue; {create: always}";
    std::string connectionOptions = argc > 3 ? argv[3] : "";
    
    Connection connection(url, connectionOptions);
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
//	cvReleaseCapture(&capture);
//	content["iplimage"]=*frame;
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
    return 1;
}
