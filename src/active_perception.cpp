/**\file active_perception.cpp
 * \brief Description...
 *
 * @version 1.0
 * @author carloscosta
 */

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <includes>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#include <active_perception/active_perception.h>
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </includes>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

namespace gazebo {

// =============================================================================  <public-section>  ============================================================================
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <constructors-destructor>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
ActivePerception::ActivePerception() :
		number_of_sampling_sensors_(100),
		number_of_intended_sensors_(1),
		elapsed_simulation_time_in_seconds_between_sensor_analysis_(1.0),
		polling_sleep_time_(0, 200000000),
		number_of_sensor_analysis_performed_(0),
		sensor_orientation_random_roll_(true),
		sensor_data_segmentation_color_rgb_(0xff00),
		number_of_sampling_sensors_pointclouds_received_(0),
		new_observation_point_available_(true),
		new_scene_model_path_available_(false),
		voxel_grid_filter_leaf_size_(0.007) {
}

ActivePerception::~ActivePerception() {
}
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </constructors-destructor>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <member-functions>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void ActivePerception::Load(physics::WorldPtr _world, sdf::ElementPtr _sdf) {
	world_ = _world;
	sdf_ = _sdf;

	// config
	if (sdf_->HasElement("numberOfSamplingSensors")) number_of_sampling_sensors_ = sdf_->GetElement("numberOfSamplingSensors")->Get<size_t>();
	if (sdf_->HasElement("numberOfIntendedSensors")) number_of_intended_sensors_ = sdf_->GetElement("numberOfIntendedSensors")->Get<size_t>();
	if (sdf_->HasElement("elapsedSimulationTimeInSecondsBetweenSensorAnalysis")) elapsed_simulation_time_in_seconds_between_sensor_analysis_ = sdf_->GetElement("elapsedSimulationTimeInSecondsBetweenSensorAnalysis")->Get<double>();
	double polling_sleep_time = polling_sleep_time_.Double();
	if (sdf_->HasElement("pollingSleepTime")) polling_sleep_time = sdf_->GetElement("pollingSleepTime")->Get<double>();
	polling_sleep_time_.Set(polling_sleep_time);
	if (sdf_->HasElement("sensorOrientationRandomRoll")) sensor_orientation_random_roll_ = sdf_->GetElement("sensorOrientationRandomRoll")->Get<bool>();

	std::string sensor_data_segmentation_color_rgb_str = "0 255 0";
	if (sdf_->HasElement("sensorDataSegmentationColorRGB")) sensor_data_segmentation_color_rgb_str = sdf_->GetElement("sensorDataSegmentationColorRGB")->Get<std::string>();
	std::stringstream sensor_data_segmentation_color_rgb_ss(sensor_data_segmentation_color_rgb_str);
	uint32_t r, g, b;
	if (sensor_data_segmentation_color_rgb_ss >> r && sensor_data_segmentation_color_rgb_ss >> g && sensor_data_segmentation_color_rgb_ss >> b) {
		sensor_data_segmentation_color_rgb_ = r << 16 | g << 8 | b;
	}

	if (sdf_->HasElement("voxelGridFilterLeafSize")) voxel_grid_filter_leaf_size_ = sdf_->GetElement("voxelGridFilterLeafSize")->Get<double>();
	if (sdf_->HasElement("sceneModelPath")) scene_model_path_ = sdf_->GetElement("sceneModelPath")->Get<std::string>();
	if (!scene_model_path_.empty()) new_scene_model_path_available_ = true;

	std::string observation_point_str = "0 0 0";
	if (sdf_->HasElement("observationPoint")) observation_point_str = sdf_->GetElement("observationPoint")->Get<std::string>();
	std::stringstream observation_point_ss(observation_point_str);
	double ox, oy, oz;
	if (observation_point_ss >> ox && observation_point_ss >> oy && observation_point_ss >> oz) {
		observation_point_.point.x = ox;
		observation_point_.point.y = oy;
		observation_point_.point.z = oz;
	}

	sdf_sensors_name_prefix_ = "active_perception";
	if (sdf_->HasElement("sdfSensorsNamePrefix")) sdf_sensors_name_prefix_ = sdf_->GetElement("sdfSensorsNamePrefix")->Get<std::string>();

	topics_sampling_sensors_prefix_ = "sampling_point_cloud_";
	if (sdf_->HasElement("topicsSamplingSensorsPrefix")) topics_sampling_sensors_prefix_ = sdf_->GetElement("topicsSamplingSensorsPrefix")->Get<std::string>();

	published_msgs_world_frame_id_ = "world";
	if (sdf_->HasElement("publishedMsgsWorldFrameId")) published_msgs_world_frame_id_ = sdf_->GetElement("publishedMsgsWorldFrameId")->Get<std::string>();

	published_msgs_frame_id_suffix_ = "_frame";
	if (sdf_->HasElement("publishedMsgsFrameIdSuffix")) published_msgs_frame_id_suffix_ = sdf_->GetElement("publishedMsgsFrameIdSuffix")->Get<std::string>();

	// ros topics
	std::string topic_observation_point = "set_observation_point";
	if (sdf_->HasElement("topicObservationPoint")) topic_observation_point = sdf_->GetElement("topicObservationPoint")->Get<std::string>();

	std::string topic_model_names = "set_model_names";
	if (sdf_->HasElement("topicModelNames")) topic_model_names = sdf_->GetElement("topicModelNames")->Get<std::string>();

	// ros init
	std::string robot_namespace = "active_perception";
	if (sdf_->HasElement("robotNamespace")) robot_namespace = sdf_->GetElement("robotNamespace")->Get<std::string>();

	if (!ros::isInitialized()) {
		int argc = 0;
		char** argv = NULL;
		ros::init(argc, argv, robot_namespace, ros::init_options::NoSigintHandler);
	}
	rosnode_.reset(new ros::NodeHandle(robot_namespace));
	rosnode_->setCallbackQueue(&queue_);

	observation_point_subscriber_ = rosnode_->subscribe(topic_observation_point, 1, &ActivePerception::ProcessNewObservationPoint, this);
	scene_model_path_subscriber_ = rosnode_->subscribe(topic_model_names, 1, &ActivePerception::ProcessNewSceneModelPath, this);
	scene_model_publisher_ = rosnode_->advertise<sensor_msgs::PointCloud2>(topics_sampling_sensors_prefix_ + "_scene_model", 1, true);
}

void ActivePerception::Init() {
	callback_queue_thread_ = boost::thread(boost::bind(&ActivePerception::QueueThread, this));
	processing_thread_ = boost::thread(boost::bind(&ActivePerception::ProcessingThread, this));
}

void ActivePerception::QueueThread() {
	while (rosnode_->ok())
		queue_.callAvailable(ros::WallDuration(0.01));
}

void ActivePerception::ProcessNewObservationPoint(const geometry_msgs::PointStampedConstPtr& _msg) {
	boost::mutex::scoped_lock scoped_lock(observation_point_mutex_);
	observation_point_ = *_msg;
	new_observation_point_available_ = true;
}

void ActivePerception::ProcessNewSceneModelPath(const std_msgs::StringConstPtr& _msg) {
	if (!_msg->data.empty()) {
		scene_model_path_mutex_.lock();
		scene_model_path_ = _msg->data;
		new_scene_model_path_available_ = true;
		scene_model_path_mutex_.unlock();
		LoadSceneModel();
	}
}

void ActivePerception::ProcessingThread() {
	LoadSensors();
	LoadSceneModel();
	SetSensorsState(true);
	world_->SetPaused(false);

	ROS_INFO_STREAM("ActivePerception has started with " << sensors_.size() << " sampling sensors for finding the optimal placement for " << number_of_intended_sensors_ << (number_of_intended_sensors_ == 1 ? " sensor" : " sensors"));
	bool sensor_analysis_required = true;

	while (rosnode_->ok()) {
		observation_point_mutex_.lock();
		if (new_observation_point_available_) {
			OrientSensorsToObservationPoint();
			new_observation_point_available_ = false;
			sensor_analysis_required = true;
		}
		observation_point_mutex_.unlock();

		if (sensor_analysis_required) {
			ROS_INFO_STREAM("Performing sensor analysis number " << number_of_sensor_analysis_performed_);
			WaitForSensorData();
			if (ProcessSensorData()) ++number_of_sensor_analysis_performed_;
			PrepareNextAnalysis();
		}
	}
}

void ActivePerception::LoadSensors() {
	sensors_.clear();
	sensors_models_.clear();
	color_image_connections_.clear();
	color_pointcloud_connections_.clear();
	sampling_sensors_pointclouds_.clear();
	sampling_sensors_pointclouds_.resize(number_of_sampling_sensors_);
	number_of_sampling_sensors_pointclouds_received_ = 0;
	sampling_sensors_color_image_publishers_.clear();
	sampling_sensors_pointcloud_publishers_.clear();

	sensors::Sensor_V sensors = sensors::SensorManager::Instance()->GetSensors();
	size_t sampling_sensors_count = CountNumberOfSamplingSensors(sensors, sdf_sensors_name_prefix_);
	while (sampling_sensors_count < number_of_sampling_sensors_) {
		sensors = sensors::SensorManager::Instance()->GetSensors();
		sampling_sensors_count = CountNumberOfSamplingSensors(sensors, sdf_sensors_name_prefix_);
		common::Time::Sleep(polling_sleep_time_);
	}

	for (size_t i = 0; i < number_of_sampling_sensors_; ++i) {
		sensors::SensorPtr sensor = sensors[i];
		if (sensor && sensor->Name().size() >= sdf_sensors_name_prefix_.size() && std::equal(sdf_sensors_name_prefix_.begin(), sdf_sensors_name_prefix_.end(), sensor->Name().begin())) {
			sensors::DepthCameraSensorPtr sensor_depth = std::dynamic_pointer_cast < sensors::DepthCameraSensor > (sensor);
			std::string sensor_parent_name_with_link = sensor_depth->ParentName();
			size_t delimiter_position = sensor_parent_name_with_link.find_first_of("::");
			if (delimiter_position != std::string::npos) {
				std::string sensor_parent_name = sensor_parent_name_with_link.substr(0, delimiter_position);
				if (!sensor_parent_name.empty()) {
					physics::ModelPtr sensor_model = world_->ModelByName(sensor_parent_name);
					if (sensor_model && sensor_depth) {
						sensors_.push_back(sensor_depth);
						sensors_models_.push_back(sensor_model);
						color_image_connections_.push_back(sensor_depth->DepthCamera()->ConnectNewImageFrame(std::bind(&ActivePerception::OnNewImageFrame,
														this, std::placeholders::_1, std::placeholders::_2,
														std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, i)));
						color_pointcloud_connections_.push_back(sensor_depth->DepthCamera()->ConnectNewRGBPointCloud(std::bind(&ActivePerception::OnNewRGBPointCloud,
								this, std::placeholders::_1, std::placeholders::_2,
								std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, i)));
						sampling_sensors_color_image_publishers_.push_back(rosnode_->advertise<sensor_msgs::Image>(topics_sampling_sensors_prefix_ + sensor_parent_name + "_color_image", 1, true));
						sampling_sensors_pointcloud_publishers_.push_back(rosnode_->advertise<sensor_msgs::PointCloud2>(topics_sampling_sensors_prefix_ + sensor_parent_name + "_pointcloud", 1, true));
					}
				}
			}
		}
	}
}

size_t ActivePerception::CountNumberOfSamplingSensors(sensors::Sensor_V& _sensors, const std::string& _sensor_name_prefix) {
	size_t sensor_count = 0;
	for (size_t i = 0; i < _sensors.size(); ++i) {
		sensors::SensorPtr sensor = _sensors[i];
		if (sensor && sensor->Name().size() >= _sensor_name_prefix.size() && std::equal(_sensor_name_prefix.begin(), _sensor_name_prefix.end(), sensor->Name().begin())) {
			++sensor_count;
		}
	}
	return sensor_count;
}

void ActivePerception::OrientSensorsToObservationPoint() {
	ignition::math::Vector3d observation_point(observation_point_.point.x, observation_point_.point.y, observation_point_.point.z);
	for (size_t i = 0; i < sensors_models_.size(); ++i) {
		math::Pose model_pose = sensors_models_[i]->GetWorldPose();
		ignition::math::Matrix4d matrix = ignition::math::Matrix4d::LookAt(ignition::math::Vector3d(model_pose.pos.x, model_pose.pos.y, model_pose.pos.z), observation_point);
		ignition::math::Quaterniond new_rotation = matrix.Rotation();
		model_pose.rot.x = new_rotation.X();
		model_pose.rot.y = new_rotation.Y();
		model_pose.rot.z = new_rotation.Z();
		model_pose.rot.w = new_rotation.W();

		if (sensor_orientation_random_roll_) {
			model_pose.rot.SetFromEuler(math::Rand::GetDblUniform(0, 2.0 * math::Angle::TwoPi.Radian()), model_pose.rot.GetPitch(), model_pose.rot.GetYaw());
		}

		sensors_models_[i]->SetWorldPose(model_pose);
	}
}

void ActivePerception::LoadSceneModel() {
	scene_model_path_mutex_.lock();
	if (new_scene_model_path_available_ && !scene_model_path_.empty()) {
		scene_model_ = typename pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>());
		if (!LoadPointCloudfromFile(scene_model_path_, *scene_model_) || !FilterPointCloud(scene_model_)) {
			scene_model_ = typename pcl::PointCloud<pcl::PointXYZ>::Ptr();
		} else {
			scene_model_->header.stamp = (pcl::uint64_t)(world_->SimTime().Double() * 1e6);
			scene_model_->header.frame_id = published_msgs_world_frame_id_;
			sensor_msgs::PointCloud2Ptr cloud_msg(new sensor_msgs::PointCloud2());
			pcl::toROSMsg(*scene_model_, *cloud_msg);
			scene_model_publisher_.publish(cloud_msg);
		}
		new_scene_model_path_available_ = false;
	}
	scene_model_path_mutex_.unlock();
}

void ActivePerception::SetSensorsState(bool _active) {
	for (size_t i = 0; i < sensors_.size(); ++i) {
		sensors_[i]->SetActive(_active);
	}
}

void ActivePerception::OnNewImageFrame(const unsigned char* _image, unsigned int _width, unsigned int _height, unsigned int _depth, const std::string& _format, size_t _sensor_index) {
	ROS_INFO_STREAM("Received color image from sensor " << _sensor_index << " with [width: " << _width << " | height: " << _height << " | depth: " << _depth << " | format: " << _format << "]");
	if (_sensor_index < sampling_sensors_color_image_publishers_.size() && sampling_sensors_color_image_publishers_[_sensor_index].getNumSubscribers() > 0) {
		sensor_msgs::Image image_msg;
		image_msg.header.stamp.fromSec(world_->SimTime().Double());
		image_msg.header.frame_id = sensors_[_sensor_index]->ParentName() + published_msgs_frame_id_suffix_;
		std::replace(image_msg.header.frame_id.begin(), image_msg.header.frame_id.end(), ':', '_');
		image_msg.width = _width;
		image_msg.height = _height;
		image_msg.step = _width * 3;
		image_msg.encoding = sensor_msgs::image_encodings::BGR8;
		image_msg.is_bigendian = false;
		size_t msg_number_bytes = _width * _height * 3;
		image_msg.data.resize(msg_number_bytes);
		memcpy(&image_msg.data[0], _image,  msg_number_bytes);
		sampling_sensors_color_image_publishers_[_sensor_index].publish(image_msg);
	}
}

void ActivePerception::OnNewRGBPointCloud(const float *_pcd,
				unsigned int _width, unsigned int _height,
				unsigned int _depth, const std::string &_format, size_t _sensor_index) {
	ROS_INFO_STREAM("Received PCD from sensor " << _sensor_index << " with [width: " << _width << " | height: " << _height << " | depth: " << _depth << " | format: " << _format << "]");
	if (_sensor_index < sensors_.size() && _sensor_index < sampling_sensors_pointclouds_.size()) {
		Eigen::Affine3f transform_sensor_to_world;
		if (GetSensorTransformToWorld(_sensor_index, transform_sensor_to_world)) {
			typename pcl::PointCloud<pcl::PointXYZ>::Ptr pointcloud = SegmentSensorDataFromDepthSensor(_pcd, _width * _height, transform_sensor_to_world);
			FilterPointCloud(pointcloud);
			pointcloud->header.stamp = (pcl::uint64_t)(world_->SimTime().Double() * 1e6);
			pointcloud->header.frame_id = published_msgs_world_frame_id_;
			PublishPointCloud(pointcloud, _sensor_index);
			if (!sampling_sensors_pointclouds_[_sensor_index]) ++number_of_sampling_sensors_pointclouds_received_;
			sampling_sensors_pointclouds_[_sensor_index] = pointcloud;
		}
	}
}

typename pcl::PointCloud<pcl::PointXYZ>::Ptr ActivePerception::SegmentSensorDataFromDepthSensor(const float* _xyzrgb_data, size_t _number_of_points, Eigen::Affine3f &_transform_sensor_to_world) {
	typename pcl::PointCloud<pcl::PointXYZ>::Ptr pointcloud(new pcl::PointCloud<pcl::PointXYZ>());
	size_t memory_index = 0;
	for (size_t i = 0; i < _number_of_points; ++i) {
		if (_xyzrgb_data[memory_index + 3] == sensor_data_segmentation_color_rgb_) {
			pointcloud->push_back(pcl::transformPoint(pcl::PointXYZ(_xyzrgb_data[memory_index], _xyzrgb_data[memory_index + 1], _xyzrgb_data[memory_index + 2]), _transform_sensor_to_world));
		}
		memory_index += 4;
	}
	return pointcloud;
}

bool ActivePerception::GetSensorTransformToWorld(size_t _sensor_index, Eigen::Affine3f &_transform_sensor_to_world) {
	if (_sensor_index < sensors_models_.size()) {
		math::Pose pose = sensors_models_[_sensor_index]->GetWorldPose();
		_transform_sensor_to_world = PoseToTransform<float>(pose);
		Eigen::Quaternion<float> rotation_ros(0.5, 0.5, -0.5, -0.5);
		_transform_sensor_to_world = _transform_sensor_to_world * rotation_ros;
		return true;
	}
	return false;
}

bool ActivePerception::FilterPointCloud(typename pcl::PointCloud<pcl::PointXYZ>::Ptr &_pointcloud) {
	typename pcl::PointCloud<pcl::PointXYZ>::Ptr pointcloud_filtered(new pcl::PointCloud<pcl::PointXYZ>());
	pcl::VoxelGrid<pcl::PointXYZ> filter;
	filter.setLeafSize(voxel_grid_filter_leaf_size_, voxel_grid_filter_leaf_size_, voxel_grid_filter_leaf_size_);
	filter.setInputCloud(_pointcloud);
	filter.filter(*pointcloud_filtered);
	_pointcloud = pointcloud_filtered;
	return _pointcloud->size() > 0;
}

bool ActivePerception::PublishPointCloud(typename pcl::PointCloud<pcl::PointXYZ>::Ptr &_pointcloud, size_t _pubisher_index) {
	if (_pointcloud && _pubisher_index < sampling_sensors_pointcloud_publishers_.size() &&
			sampling_sensors_pointcloud_publishers_[_pubisher_index].getNumSubscribers() > 0) {
		sensor_msgs::PointCloud2Ptr cloud_msg(new sensor_msgs::PointCloud2());
		pcl::toROSMsg(*_pointcloud, *cloud_msg);
		sampling_sensors_pointcloud_publishers_[_pubisher_index].publish(cloud_msg);
		return true;
	}
	return false;
}

void ActivePerception::WaitForSensorData() {
	while (number_of_sampling_sensors_pointclouds_received_ < sampling_sensors_pointclouds_.size()) {
		common::Time::Sleep(polling_sleep_time_);
	}
}

bool ActivePerception::ProcessSensorData() {
	while (!scene_model_) {
		ROS_WARN("Waiting for scene model");
		common::Time::Sleep(polling_sleep_time_);
	}

	return false;
}

void ActivePerception::PrepareNextAnalysis() {
	number_of_sampling_sensors_pointclouds_received_ = 0;
	sampling_sensors_pointclouds_.clear();
	sampling_sensors_pointclouds_.resize(number_of_sampling_sensors_);
}
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </member-functions>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// =============================================================================  </public-section>  ===========================================================================

// =============================================================================   <protected-section>   =======================================================================
// =============================================================================   </protected-section>  =======================================================================

} /* namespace gazebo */
