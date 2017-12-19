/**
* This file is part of LSD-SLAM.
*
* Copyright 2013 Jakob Engel <engelj at in dot tum dot de> (Technical University of Munich)
* For more information see <http://vision.in.tum.de/lsdslam> 
*
* LSD-SLAM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* LSD-SLAM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with dvo. If not, see <http://www.gnu.org/licenses/>.
*/

#define GL_GLEXT_PROTOTYPES 1
#include "PointCloudViewer.h"
#include "qfiledialog.h"
#include "qcoreapplication.h"
#include <stdio.h>
#include "settings.h"
#include "ros/package.h"

#include <zlib.h>
#include <iostream>


#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include "QGLViewer/manipulatedCameraFrame.h"

#include "KeyFrameDisplay.h"
#include "KeyFrameGraphDisplay.h"

#include <iostream>
#include <fstream>

#include <math.h>
#include <stdlib.h> // RAND_MAX
#include "main_viewer.h"


using namespace qglviewer;
using namespace std;



PointCloudViewer::PointCloudViewer()
{
	setPathKey(Qt::Key_0,0);
	setPathKey(Qt::Key_1,1);
	setPathKey(Qt::Key_2,2);
	setPathKey(Qt::Key_3,3);
	setPathKey(Qt::Key_4,4);
	setPathKey(Qt::Key_5,5);
	setPathKey(Qt::Key_6,6);
	setPathKey(Qt::Key_7,7);
	setPathKey(Qt::Key_8,8);
	setPathKey(Qt::Key_9,9);


	currentCamDisplay = 0;
	graphDisplay = 0;


	for(int i=0;i<10;i++)
	{
		KFexists[i] = 0;
		KFautoPlayIdx[i] = -1;
	}

	kfInt = new qglviewer::KeyFrameInterpolator(new qglviewer::Frame());
	customAnimationEnabled = false;

	setSnapshotFormat(QString("PNG"));

	reset();//重置
}


PointCloudViewer::~PointCloudViewer()
{
	delete currentCamDisplay;
	delete graphDisplay;
}


void PointCloudViewer::reset()
{
	if(currentCamDisplay != 0)
		delete currentCamDisplay;
	if(graphDisplay != 0)
		delete graphDisplay;

	currentCamDisplay = new KeyFrameDisplay();
	graphDisplay = new KeyFrameGraphDisplay();

	KFcurrent = 0;
	KFLastPCSeq = -1;

	resetRequested=false;

	save_folder = ros::package::getPath("lsd_slam_viewer")+"/save/";
	localMsBetweenSaves = 1;
	simMsBetweenSaves = 1;
	lastCamID = -1;
	lastAnimTime = lastCamTime = lastSaveTime = 0;
	char buf[500];
	snprintf(buf,500,"rm -rf %s",save_folder.c_str());
	int k = system(buf);
	snprintf(buf,500,"mkdir %s",save_folder.c_str());
	k += system(buf);


	assert(k != -42);

	setSceneRadius(80);
	setTextIsEnabled(false);
	lastAutoplayCheckedSaveTime = -1;

	animationPlaybackEnabled = false;
}

void PointCloudViewer::addFrameMsg(lsd_slam_viewer::keyframeMsgConstPtr msg)
{
	meddleMutex.lock();
    
//  displays kf-graph
// 	KeyFrameGraphDisplay* graphDisplay;
// 
// 	displays only current keyframe (which is not yet in the graph).
// 	KeyFrameDisplay* currentCamDisplay;

	if(!msg->isKeyframe)//不是关键帧
	{
		if(currentCamDisplay->id > msg->id)
		{
			printf("detected backward-jump in id (%d to %d), resetting!\n", currentCamDisplay->id, msg->id);
			resetRequested = true;
		}
		currentCamDisplay->setFrom(msg);
		lastAnimTime = lastCamTime = msg->time;
		lastCamID = msg->id;
	}
	else//是关键帧
		graphDisplay->addMsg(msg);//添加关键真的KeyFrameDisplay信息

	meddleMutex.unlock();
}

void PointCloudViewer::addGraphMsg(lsd_slam_viewer::keyframeGraphMsgConstPtr msg)
{
	meddleMutex.lock();

	graphDisplay->addGraphMsg(msg);//添加关键真之间的约束

	meddleMutex.unlock();
}




void PointCloudViewer::init()
{
    robot_pose << 0,0,0;
    pose_pi    <<   1,0,0,
                    0,1,0,
                    0,0,1;
    pose_sigma << 0,0,0;
    cout.setf(ios::fixed);
	setAnimationPeriod(30);//设置频率，这个很重要，后期需要调整
	startAnimation();
}

QString PointCloudViewer::helpString() const
{
	return QString("");
}

void PointCloudViewer::draw()
{
	meddleMutex.lock();


	if(resetRequested)
	{
		reset();
		resetRequested = false;
	}


	glPushMatrix();


	if(animationPlaybackEnabled)
	{
		double tm = ros::Time::now().toSec() - animationPlaybackTime;

		if(tm > kfInt->lastTime())
		{
			animationPlaybackEnabled = false;
			tm = kfInt->lastTime();
		}

		if(tm < kfInt->firstTime())
			tm = kfInt->firstTime();

		printf("anim at %.2f (%.2f to %.2f)\n", tm, kfInt->firstTime(), kfInt->lastTime());


		kfInt->interpolateAtTime(tm);
		camera()->frame()->setFromMatrix(kfInt->frame()-> matrix());



		double accTime = 0;
		for(unsigned int i=0;i<animationList.size();i++)
		{
			if(tm >= accTime && tm < accTime+animationList[i].duration && animationList[i].isFix)
			{
				camera()->frame()->setFromMatrix(animationList[i].frame.matrix());

				printf("fixFrameto %d at %.2f (%.2f to %.2f)\n", i, tm, kfInt->firstTime(), kfInt->lastTime());
			}

			accTime += animationList[i].duration;
		}


		accTime = 0;
		AnimationObject* lastAnimObj = 0;
		for(unsigned int i=0;i<animationList.size();i++)
		{
			accTime += animationList[i].duration;
			if(animationList[i].isSettings && accTime <= tm)
				lastAnimObj = &(animationList[i]);
		}
		if(lastAnimObj != 0)
		{
			absDepthVarTH = lastAnimObj->absTH;
			scaledDepthVarTH = lastAnimObj->scaledTH;
			minNearSupport = lastAnimObj->neighb;
			sparsifyFactor = lastAnimObj->sparsity;
			showKFCameras = lastAnimObj->showKeyframes;
			showConstraints = lastAnimObj->showLoopClosures;
		}
	}



	if(showCurrentCamera)
    {
        if(currentCamDisplay->id > last_frame_id)
        {
            printf("This PC frame is %d\n",currentCamDisplay->id);
            Sophus::Sim3f POSE = currentCamDisplay->camToWorld;
            //std::cout << "POSE is \n\n"<<  POSE.matrix() << "\n\n";
            
            Sophus::Matrix3f Rotation = POSE.rotationMatrix();
            //cout << "Rotation is \n\n"<< Rotation << "\n\n";
            
            Sophus::Vector3f Translation = POSE.translation();
            //cout << "Translation is \n\n"<< Translation << "\n\n";
            
            pose_pi *= Rotation.transpose();
            
            pose_sigma += pose_pi*Translation;
            
            robot_pose = -pose_sigma;
            
            cout << setprecision(8) << "Now This Robot in\n\n"<< robot_pose << "\n\n";
        }
        
        
        
		currentCamDisplay->drawCam(2*lineTesselation, 0);
        last_frame_id = currentCamDisplay->id;
    }

	if(showCurrentPointcloud)
		currentCamDisplay->drawPC(pointTesselation, 1);


	graphDisplay->draw();


	glPopMatrix();

	meddleMutex.unlock();




	if(saveAllVideo)
	{
		double span = ros::Time::now().toSec() - lastRealSaveTime;
		if(span > 0.4)
		{
			setSnapshotQuality(100);

			printf("saved (img %d @ time %lf, saveHZ %f)!\n", lastCamID, lastAnimTime, 1.0/localMsBetweenSaves);

			char buf[500];
			snprintf(buf,500,"%s%lf.png",save_folder.c_str(),  ros::Time::now().toSec());
			saveSnapshot(QString(buf));
			lastRealSaveTime = ros::Time::now().toSec();
		}


	}
}

void PointCloudViewer::keyReleaseEvent(QKeyEvent *e)
  {

  }


void PointCloudViewer::setToVideoSize()
{
	this->setFixedSize(1600,900);
}


void PointCloudViewer::remakeAnimation()
{
	delete kfInt;
	kfInt = new qglviewer::KeyFrameInterpolator(new qglviewer::Frame());
	std::sort(animationList.begin(), animationList.end());

	float tm=0;
	for(unsigned int i=0;i<animationList.size();i++)
	{
	  if(!animationList[i].isSettings)
	  {
		  kfInt->addKeyFrame(&animationList[i].frame, tm);
		  tm += animationList[i].duration;
	  }
	}

	printf("made animation with %d keyframes, spanning %f s!\n", kfInt->numberOfKeyFrames(), tm);
}

void PointCloudViewer::keyPressEvent(QKeyEvent *e)
  {
    switch (e->key())
    {
      case Qt::Key_S :
    	    setToVideoSize();
    	  break;

      case Qt::Key_R :
    	    resetRequested = true;

    	  break;

      case Qt::Key_T:	// add settings item
    	  meddleMutex.lock();
    	  animationList.push_back(AnimationObject(true, lastAnimTime, 0));
    	  meddleMutex.unlock();
    	  printf("added St: %s\n", animationList.back().toString().c_str());

    	  break;

      case Qt::Key_K:	// add keyframe item
    	  meddleMutex.lock();


    	  float x,y,z;
    	  camera()->frame()->getPosition(x,y,z);
    	  animationList.push_back(AnimationObject(false, lastAnimTime, 2, qglviewer::Frame(qglviewer::Vec(0,0,0), camera()->frame()->orientation())));
    	  animationList.back().frame.setPosition(x,y,z);
    	  meddleMutex.unlock();
    	  printf("added KF: %s\n", animationList.back().toString().c_str());



    	  remakeAnimation();

    	  break;

      case Qt::Key_I :	// reset animation list
			meddleMutex.lock();
			animationList.clear();
			meddleMutex.unlock();
			printf("resetted animation list!\n");

			remakeAnimation();

    	  break;


      case Qt::Key_F :	// save list
      {
			meddleMutex.lock();
			std::ofstream myfile;
			myfile.open ("animationPath.txt");
			for(unsigned int i=0;i<animationList.size();i++)
			{
				myfile << animationList[i].toString() << "\n";
			}
			myfile.close();
			meddleMutex.unlock();

			printf("saved animation list (%d items)!\n", (int)animationList.size());
      }
    	  break;


      case Qt::Key_L :	// load list
      {
			meddleMutex.lock();
			animationList.clear();

			std::ifstream myfile;
			std::string line;
			myfile.open ("animationPath.txt");

			if (myfile.is_open())
			{
				while ( getline (myfile,line) )
				{
					if(!(line[0] == '#'))
						animationList.push_back(AnimationObject(line));
				}
				myfile.close();
			}
			else
				std::cout << "Unable to open file";
			myfile.close();
			meddleMutex.unlock();

			printf("loaded animation list! (%d items)!\n", (int)animationList.size());
			remakeAnimation();
      }
    	  break;


      case Qt::Key_A:
    	  if(customAnimationEnabled)
    		  printf("DISABLE custom animation!\n)");
    	  else
    		  printf("ENABLE custom animation!\n");
    	  customAnimationEnabled = !customAnimationEnabled;
    	  break;

      case Qt::Key_O:
    	  if(animationPlaybackEnabled)
    	  {
    		  animationPlaybackEnabled=false;
    	  }
    	  else
    	  {
    		  animationPlaybackEnabled = true;
    		  animationPlaybackTime = ros::Time::now().toSec();
    	  }
      	  break;


      case Qt::Key_P:
    	  graphDisplay->flushPointcloud = true;
    	  break;

      case Qt::Key_W:
    	  graphDisplay->printNumbers = true;
    	  break;

      default:
    	  QGLViewer::keyPressEvent(e);
    	  break;
    }
  }
  
///////////////////////RobotViewer  ///////////////////////
void RobotViewer::init() 
{
    restoreStateFromFile();
    glDisable(GL_LIGHTING);
    nbPart_ = 2000;
    particle_ = new Particle[nbPart_];
    glPointSize(8.0);
    setGridIsDrawn();
    robot_pose << 0,0,0;
    //help();
    //setAnimationPeriod(30);//设置频率，这个很重要，后期需要调整
    startAnimation();
    printf("Init OK!\n");
}

void RobotViewer::draw() 
{
    {
//     float width = viewer->currentCamDisplay->width;
//     float height = viewer->currentCamDisplay->height;
//     if(width == 0)
// 		return;
//     float cx = viewer->currentCamDisplay->cx;
//     float cy = viewer->currentCamDisplay->cy;
//     float fx = viewer->currentCamDisplay->fx;
//     float fy = viewer->currentCamDisplay->fy;
//     
// //     float cx = 1;
// //     float cy = 1;
// //     float fx = 1;
// //     float fy = 1;
//     
//     //std::cout <<  camToWorld.matrix() << "\n\n";
// 	glPushMatrix();
// 
// 		Sophus::Matrix4f m = viewer->currentCamDisplay->camToWorld.matrix();
//         
// 		glMultMatrixf((GLfloat*)m.data());//上载定义的矩阵m，右乘
// 
// 		
//         glColor3f(1,0,0);
// 		
// 
// 		glLineWidth(4);
// 		glBegin(GL_LINES);
//  		glVertex3f(0,0,0);//(0,0,0)为空间坐标原点，即万事万物都是从这一点开始观察的，这个就是小孔成像的那个孔
//  		glVertex3f(0.05*(0-cx)/fx,0.05*(0-cy)/fy,0.05);
//         
// 		glVertex3f(0,0,0);
// 		glVertex3f(0.05*(0-cx)/fx,0.05*(height-1-cy)/fy,0.05);
//         
// 		glVertex3f(0,0,0);
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(height-1-cy)/fy,0.05);
//         
// 		glVertex3f(0,0,0);
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(0-cy)/fy,0.05);
// 
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(0-cy)/fy,0.05);
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(height-1-cy)/fy,0.05);
// 
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(height-1-cy)/fy,0.05);
// 		glVertex3f(0.05*(0-cx)/fx,0.05*(height-1-cy)/fy,0.05);
// 
// 		glVertex3f(0.05*(0-cx)/fx,0.05*(height-1-cy)/fy,0.05);
// 		glVertex3f(0.05*(0-cx)/fx,0.05*(0-cy)/fy,0.05);
// 
// 		glVertex3f(0.05*(0-cx)/fx,0.05*(0-cy)/fy,0.05);
// 		glVertex3f(0.05*(width-1-cx)/fx,0.05*(0-cy)/fy,0.05);
// 
//         //八条直线就组成了小电视
// 		glEnd();
// 	glPopMatrix();
    }
    //printf("draw OK!\n");
  
//   //if(viewer->currentCamDisplay->id > frame_id)
//     {
//         Sophus::Sim3f POSE = viewer->currentCamDisplay->camToWorld;
//         //cout << "POSE is \n\n"<< POSE << "\n\n";
//         //std::cout << "POSE is \n\n"<<  POSE.matrix() << "\n\n";
//         
//         Sophus::Matrix3f Rotation = POSE.rotationMatrix();
//         //Sophus::Matrix3f Rotation = POSE.block(0,0,3,3);
//         //cout << "Rotation is \n\n"<< Rotation << "\n\n";
//         
//         Sophus::Vector3f Translation = POSE.translation();
//         //cout << "Translation is \n\n"<< Translation << "\n\n";
//         
//         
        robot_pose = viewer->robot_pose;
        
        //matrix.block<p,q>(i,j)
        
        //cout << "This Robot in\n\n"<< robot_pose << "\n\n";
        
        glColor3f(1,0,0);
        glLineWidth(4);
        glBegin(GL_LINES);//GL_POINTS
            
            //glVertex3f(100,120,120);
            glVertex3f(0,0,0);
            glVertex3f(robot_pose[0],robot_pose[1],robot_pose[2]);
        glEnd();
    //}
    
    
    //frame_id = viewer->currentCamDisplay->id;

    
}

void RobotViewer::animate() 
{
    //printf("animate OK!\n");
//     for (int i = 0; i < nbPart_; i++)
//         particle_[i].animate();
}

QString RobotViewer::helpString() const 
{
    QString text("<h2>A n i m a t i o n</h2>");
    text += "Use the <i>animate()</i> function to implement the animation part "
            "of your ";
    text += "application. Once the animation is started, <i>animate()</i> and "
            "<i>draw()</i> ";
    text += "are called in an infinite loop, at a frequency that can be "
            "fixed.<br><br>";
    text += "Press <b>Return</b> to start/stop the animation.";
    return text;
}

///////////////////////   P a r t i c l e   ///////////////////////////////

Particle::Particle() { init(); }

void Particle::animate() 
{
    speed_.z -= 0.05f;
    pos_ += 0.1f * speed_;

    if (pos_.z < 0.0) {
        speed_.z = -0.8 * speed_.z;
        pos_.z = 0.0;
    }

    if (++age_ == ageMax_)
        init();
}

void Particle::draw() 
{
    glColor3f(age_ / (float)ageMax_, age_ / (float)ageMax_, 1.0);
    glVertex3fv(pos_);
}

void Particle::init() 
{
    pos_ = Vec(0.0, 0.0, 0.0);
    float angle = 2.0 * M_PI * rand() / RAND_MAX;
    float norm = 0.04 * rand() / RAND_MAX;
    speed_ = Vec(norm * cos(angle), norm * sin(angle),
                rand() / static_cast<float>(RAND_MAX));
    age_ = 0;
    ageMax_ = 50 + static_cast<int>(100.0 * rand() / RAND_MAX);
}

