/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2016  <copyright holder> <email>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "pointcloudmapping.h"
#include <KeyFrame.h>
#include <opencv2/highgui/highgui.hpp>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/filters/passthrough.h>
#include <pcl/common/transforms.h>
#include <pcl/io/pcd_io.h>
#include "Converter.h"
#include "Eigen/Dense" 
#include "Eigen/Geometry"


pcl::PointCloud<pcl::PointXYZRGBA> pcl_cloud;



PointCloudMapping::PointCloudMapping(double resolution_)
{
    this->resolution = resolution_;
    voxel.setLeafSize( resolution, resolution, resolution);

    this->sor.setMeanK(50);                                //设置在进行统计时考虑查询点邻近点数
    this->sor.setStddevMulThresh(1.0);                    //设置判断是否为离群点的阈值

    globalMap = boost::make_shared< PointCloud >( );
    
    viewerThread = make_shared<thread>( bind(&PointCloudMapping::viewer, this ) );
	
}

void PointCloudMapping::shutdown()
{
    {
        unique_lock<mutex> lck(shutDownMutex);
        shutDownFlag = true;
        keyFrameUpdated.notify_one();
    }
    viewerThread->join();
}

void PointCloudMapping::insertKeyFrame(KeyFrame* kf, cv::Mat& color, cv::Mat& depth)
{
    cout<<"receive a keyframe, id = "<<kf->mnId<<endl;
    unique_lock<mutex> lck(keyframeMutex);
    keyframes.push_back( kf );
    colorImgs.push_back( color.clone() );
    depthImgs.push_back( depth.clone() );
    
    keyFrameUpdated.notify_one();
}

pcl::PointCloud< PointCloudMapping::PointT >::Ptr PointCloudMapping::generatePointCloud(KeyFrame* kf, cv::Mat& color, cv::Mat& depth)
{
    PointCloud::Ptr tmp( new PointCloud() );
    // point cloud is null ptr
    for ( int m=0; m<depth.rows; m+=3 )
    {
        for ( int n=0; n<depth.cols; n+=3 )
        {
            float d = depth.ptr<float>(m)[n];
            if (d < 0.01 || d>10)
                continue;
            PointT p;
            p.z = d;
            p.x = ( n - kf->cx) * p.z / kf->fx;
            p.y = ( m - kf->cy) * p.z / kf->fy;
            
            p.b = color.ptr<uchar>(m)[n*3];
            p.g = color.ptr<uchar>(m)[n*3+1];
            p.r = color.ptr<uchar>(m)[n*3+2];
			tmp->points.push_back(p);
        }
    }
    
    Eigen::Isometry3d T = ORB_SLAM2::Converter::toSE3Quat( kf->GetPose() );
    PointCloud::Ptr cloud(new PointCloud);
    pcl::transformPointCloud( *tmp, *cloud, T.inverse().matrix());
    cloud->is_dense = false;
    
    cout<<"generate point cloud for kf "<<kf->mnId<<", size="<<cloud->points.size()<<endl;
    return cloud;
}


void PointCloudMapping::viewer()
{
    pcl::visualization::CloudViewer viewer("viewer");
    while(1)
    {
        {
            unique_lock<mutex> lck_shutdown( shutDownMutex );
            if (shutDownFlag)
            {
                break;
            }
        }
        {
            unique_lock<mutex> lck_keyframeUpdated( keyFrameUpdateMutex );
            keyFrameUpdated.wait( lck_keyframeUpdated );
        }
        
        // keyframe is updated 
        size_t N=0;
        {
            unique_lock<mutex> lck( keyframeMutex );
            N = keyframes.size();
        }
        
        for ( size_t i=lastKeyframeSize; i<N ; i++ )
        {
            PointCloud::Ptr p = generatePointCloud( keyframes[i], colorImgs[i], depthImgs[i] );
            *globalMap += *p;
        }
        PointCloud::Ptr tmp(new PointCloud());
        voxel.setInputCloud( globalMap );
        voxel.filter( *tmp );
        globalMap->swap( *tmp );
		
		PointCloud::Ptr Trans(new PointCloud());
		Cloud_transform(globalMap, Trans);
		
		pcl_cloud = *Trans;
		Trans = pcl_cloud.makeShared();
		
        viewer.showCloud( Trans );                                        // show the pointcloud without optimization
        cout<<"show global map, size="<<globalMap->points.size()<<endl;
        lastKeyframeSize = N;
    }

    globalMap->clear();
    for(size_t i=0;i<keyframes.size();i++)                               // save the optimized pointcloud
    {
        cout<<"keyframe "<<i<<" ..."<<endl;
        PointCloud::Ptr tp = generatePointCloud( keyframes[i], colorImgs[i], depthImgs[i] );
        PointCloud::Ptr tmp(new PointCloud());
        voxel.setInputCloud( tp );
        voxel.filter( *tmp );
        *globalMap += *tmp;
        viewer.showCloud( globalMap );
    }
    PointCloud::Ptr tmp(new PointCloud());
    sor.setInputCloud(globalMap);
    sor.filter(*tmp);
    globalMap->swap( *tmp );         
    pcl::io::savePCDFileBinary ( "optimized_pointcloud.pcd", *globalMap );
    cout<<"Save point cloud file successfully!"<<endl;
}

void PointCloudMapping::public_cloud(pcl::PointCloud< pcl::PointXYZRGBA >& cloud)
{
	pcl::PointCloud<pcl::PointXYZRGBA> pcl_cloud_trans;
	cloud = pcl_cloud_trans;
}

void PointCloudMapping::Cloud_transform(pcl::PointCloud< PointCloudMapping::PointT >::Ptr& source, pcl::PointCloud< PointCloudMapping::PointT >::Ptr& out)
{
	Eigen::Affine3f transform = Eigen::Affine3f::Identity();
	PointCloud::Ptr cloud_filtered(new PointCloud());
	transform.translation() << 0.0, 0.0, 0.5;
	transform.rotate (Eigen::AngleAxisf (1.5*M_PI, Eigen::Vector3f::UnitX()));
	pcl::transformPointCloud (*source, *cloud_filtered, transform);
	
	//直通滤波器
	pass.setInputCloud (cloud_filtered);            //设置输入点云
	pass.setFilterFieldName ("z");         	   //设置过滤时所需要点云类型的Z字段
	pass.setFilterLimits (-1.5, 1.0);
	pass.filter (*out); 
}
/*
	Eigen::Matrix4f transform_1 = Eigen::Matrix4f::Identity();
	Eigen::Vector3d T(0, 0, 0.35);
	Matrix3f m;
	m = AngleAxisf(angle1, Vector3f::UnitZ())
	  * AngleAxisf(angle2, Vector3f::UnitY())
	  * AngleAxisf(angle3, Vector3f::UnitZ());
	transform_1.block<3,3>(0,0) = m;
	

*/
