#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include<opencv2/core/core.hpp>
#include <image_transport/image_transport.h>
#include <System.h>
#include"../ORB_SLAM2_modified/include/System.h"
#include"../ORB_SLAM2_modified/include/pointcloudmapping.h"
#include <tf/transform_broadcaster.h>
#include "geometry_msgs/PoseWithCovarianceStamped.h"


tf::Transform orb_slam;
std::vector<float> Pose_quat(4);
std::vector<float> Pose_trans(3);
tf::TransformBroadcaster * orb_slam_broadcaster;
geometry_msgs::PoseStamped Cam_Pose;
ros::Publisher CamPose_Pub;
ros::Publisher Camodom_Pub;
cv::Mat Camera_Pose;
geometry_msgs::PoseWithCovarianceStamped Cam_odom;

void Pub_CamPose(cv::Mat &pose);

void imageCallback(const sensor_msgs::ImageConstPtr& msg)
{
	try
	{
		Camera_Pose = cv_bridge::toCvShare(msg, "32FC1")->image.clone();
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("Could not convert from '%s' to 'bgr8'.", msg->encoding.c_str());
	}
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "offb_vel");
    ros::NodeHandle nh;
	image_transport::ImageTransport it(nh);
	image_transport::Subscriber sub = it.subscribe("/camera/Tcw", 1, imageCallback);
	CamPose_Pub = nh.advertise<geometry_msgs::PoseStamped>("/Camera_Pose",1);
	Camodom_Pub = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("/Camera_odom", 1);
    ros::Rate loop_rate(100);
	
    while(ros::ok())
    {
		Pub_CamPose(Camera_Pose);
//		ROS_INFO("size;%d",Camera_Pose.size());

        ros::spinOnce();
        loop_rate.sleep();
    }
    return 0;
}

void Pub_CamPose(cv::Mat &pose)
{
	cv::Mat Rwc(3,3,CV_32F);
	cv::Mat twc(3,1,CV_32F);
	Eigen::Matrix<double,3,3> rotationMat;
	orb_slam_broadcaster = new tf::TransformBroadcaster;
	if(pose.dims<2)
	{
		 ROS_INFO("Waiting fot data");
		 Rwc = Rwc;
		 twc = twc;
	}
	else
	{
		Rwc = pose.rowRange(0,3).colRange(0,3).t();
		twc = -Rwc*pose.rowRange(0,3).col(3);
		
		rotationMat << Rwc.at<float>(0,0), Rwc.at<float>(0,1), Rwc.at<float>(0,2),
					Rwc.at<float>(1,0), Rwc.at<float>(1,1), Rwc.at<float>(1,2),
					Rwc.at<float>(2,0), Rwc.at<float>(2,1), Rwc.at<float>(2,2);
		Eigen::Quaterniond Q(rotationMat);
//		Eigen::Quaterniond Q_Modify(1.4142, 0, 1.4142, 0);
//		Q = Q*Q_Modify.inverse();
		Pose_quat[0] = Q.x(); Pose_quat[1] = Q.y();
		Pose_quat[2] = Q.z(); Pose_quat[3] = Q.w();
		
		Pose_trans[0] = twc.at<float>(0);
		Pose_trans[1] = twc.at<float>(1);
		Pose_trans[2] = twc.at<float>(2);
		
		orb_slam.setOrigin(tf::Vector3(Pose_trans[0], Pose_trans[2], -Pose_trans[1]));
		orb_slam.setRotation(tf::Quaternion(Q.z(), -Q.x(), -Q.y(), Q.w()));
		orb_slam_broadcaster->sendTransform(tf::StampedTransform(orb_slam, ros::Time::now(), "/map", "/odom"));
		
		Cam_Pose.header.stamp = ros::Time::now();
		Cam_Pose.header.frame_id = "/camera_rgb_frame";
		tf::pointTFToMsg(orb_slam.getOrigin(), Cam_Pose.pose.position);
		tf::quaternionTFToMsg(orb_slam.getRotation(), Cam_Pose.pose.orientation);
		
		Cam_odom.header.stamp = ros::Time::now();
		Cam_odom.header.frame_id = "/camera_rgb_frame";
		tf::pointTFToMsg(orb_slam.getOrigin(), Cam_odom.pose.pose.position);
		tf::quaternionTFToMsg(orb_slam.getRotation(), Cam_odom.pose.pose.orientation);
		Cam_odom.pose.covariance = {0.01, 0, 0, 0, 0, 0,
									0, 0.01, 0, 0, 0, 0,
									0, 0, 0.01, 0, 0, 0,
									0, 0, 0, 0.01, 0, 0,
									0, 0, 0, 0, 0.01, 0,
									0, 0, 0, 0, 0, 0.01};
		
		CamPose_Pub.publish(Cam_Pose);
		Camodom_Pub.publish(Cam_odom);
	}
}






