/*************************************************************************
This software allows for filtering in high-dimensional observation and
state spaces, as described in

M. Wuthrich, P. Pastor, M. Kalakrishnan, J. Bohg, and S. Schaal.
Probabilistic Object Tracking using a Range Camera
IEEE/RSJ Intl Conf on Intelligent Robots and Systems, 2013

In a publication based on this software pleace cite the above reference.


Copyright (C) 2014  Manuel Wuthrich

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*************************************************************************/
#include <ros/package.h>

#include <dbot/utils/profiling.hpp>

#include <dbot_ros_pkg/trackers/tracker.hpp>
#include <dbot_ros_pkg/utils/ros_interface.hpp>
#include <dbot_ros_pkg/utils/object_file_reader.hpp>

//#include <dbot_ros_pkg/utils/cloud_visualizer.hpp>
#include <dbot_ros_pkg/ObjectState.h>

#include <boost/filesystem.hpp>


MultiObjectTracker::MultiObjectTracker():
        node_handle_("~"),
        last_measurement_time_(std::numeric_limits<Scalar>::quiet_NaN())
{
    ri::ReadParameter("object_names", object_names_, node_handle_);
    ri::ReadParameter("downsampling_factor", downsampling_factor_, node_handle_);
    object_marker_publisher_ = node_handle_.advertise<visualization_msgs::Marker>("object_model", 0);
    object_state_publisher_ = node_handle_.advertise<dbot_ros_pkg::ObjectState>("object_state", 0);
}

void MultiObjectTracker::Initialize(
        std::vector<Eigen::VectorXd> initial_states,
        const sensor_msgs::Image& ros_image,
        Eigen::Matrix3d camera_matrix,
        bool state_is_partial)
{
    boost::mutex::scoped_lock lock(mutex_);


    std::cout << "received " << initial_states.size() << " intial states " << std::endl;
    // convert camera matrix and image to desired format
    camera_matrix.topLeftCorner(2,3) /= double(downsampling_factor_);
    Observation image = ri::Ros2Eigen<Scalar>(ros_image, downsampling_factor_); // convert to meters

    // read some parameters
    bool use_gpu; ri::ReadParameter("use_gpu", use_gpu, node_handle_);
    int evaluation_count; ri::ReadParameter("evaluation_count", evaluation_count, node_handle_);
    std::vector<std::vector<size_t> > sampling_blocks; ri::ReadParameter("sampling_blocks", sampling_blocks, node_handle_);
    double max_kl_divergence; ri::ReadParameter("max_kl_divergence", max_kl_divergence, node_handle_);

    int max_sample_count; ri::ReadParameter("max_sample_count", max_sample_count, node_handle_);

    double initial_occlusion_prob; ri::ReadParameter("initial_occlusion_prob", initial_occlusion_prob, node_handle_);
    double p_occluded_visible; ri::ReadParameter("p_occluded_visible", p_occluded_visible, node_handle_);
    double p_occluded_occluded; ri::ReadParameter("p_occluded_occluded", p_occluded_occluded, node_handle_);

    double linear_acceleration_sigma; ri::ReadParameter("linear_acceleration_sigma", linear_acceleration_sigma, node_handle_);
    double angular_acceleration_sigma; ri::ReadParameter("angular_acceleration_sigma", angular_acceleration_sigma, node_handle_);
    double damping; ri::ReadParameter("damping", damping, node_handle_);

    double tail_weight; ri::ReadParameter("tail_weight", tail_weight, node_handle_);
    double model_sigma; ri::ReadParameter("model_sigma", model_sigma, node_handle_);
    double sigma_factor; ri::ReadParameter("sigma_factor", sigma_factor, node_handle_);

    double delta_time = 0.033;

    std::cout << "sampling blocks: " << std::endl;
    ff::hf::PrintVector(sampling_blocks);

    // load object mesh
    std::vector<std::vector<Eigen::Vector3d> > object_vertices(object_names_.size());
    std::vector<std::vector<std::vector<int> > > object_triangle_indices(object_names_.size());
    std::vector<dbot_ros_pkg::ObjectState> object_states(object_names_.size());
    for(size_t i = 0; i < object_names_.size(); i++)
    {
        std::string object_model_path = ros::package::getPath("dbot_ros_pkg") +
                "/object_models/"  + object_names_[i] + ".obj";
        ObjectFileReader file_reader;
        file_reader.set_filename(object_model_path);
        file_reader.Read();

        object_vertices[i] = *file_reader.get_vertices();
        object_triangle_indices[i] = *file_reader.get_indices();
    }

   ri::ReadParameter("update_rate", update_rate_, node_handle_);


    boost::shared_ptr<State> rigid_bodies_state(new ff::FreeFloatingRigidBodiesState<>(object_names_.size()));
    boost::shared_ptr<ff::RigidBodyRenderer> object_renderer(new ff::RigidBodyRenderer(
                                                                      object_vertices,
                                                                      object_triangle_indices,
                                                                      rigid_bodies_state));

    // initialize observation model ========================================================================================================================================================================================================================================================================================================================================================================================================================
    boost::shared_ptr<ObservationModel> observation_model;

    // cpu obseration model
    boost::shared_ptr<ff::KinectPixelObservationModel> kinect_pixel_observation_model(
                new ff::KinectPixelObservationModel(tail_weight, model_sigma, sigma_factor));
    boost::shared_ptr<ff::OcclusionProcessModel> occlusion_process(
                new ff::OcclusionProcessModel(p_occluded_visible, p_occluded_occluded));
    observation_model = boost::shared_ptr<ObservationModelCPUType>(
                new ObservationModelCPUType(camera_matrix,
                                            image.rows(),
                                            image.cols(),
                                            initial_states.size(),
                                            object_renderer,
                                            kinect_pixel_observation_model,
                                            occlusion_process,
                                            initial_occlusion_prob,
                                            delta_time));



    std::cout << "initialized observation omodel " << std::endl;

    // initialize process model ========================================================================================================================================================================================================================================================================================================================================================================================================================
    Eigen::MatrixXd linear_acceleration_covariance = Eigen::MatrixXd::Identity(3, 3) * pow(double(linear_acceleration_sigma), 2);
    Eigen::MatrixXd angular_acceleration_covariance = Eigen::MatrixXd::Identity(3, 3) * pow(double(angular_acceleration_sigma), 2);

    boost::shared_ptr<ProcessModel> process(new ProcessModel(delta_time, object_names_.size()));
    for(size_t i = 0; i < object_names_.size(); i++)
    {
        process->Parameters(i, object_renderer->object_center(i).cast<double>(),
                               damping,
                               linear_acceleration_covariance,
                               angular_acceleration_covariance);
    }

    std::cout << "initialized process model " << std::endl;
    // initialize coordinate_filter ============================================================================================================================================================================================================================================================
    filter_ = boost::shared_ptr<FilterType>
            (new FilterType(process, observation_model, sampling_blocks, max_kl_divergence));

    // for the initialization we do standard sampling
    std::vector<std::vector<size_t> > dependent_sampling_blocks(1);
    dependent_sampling_blocks[0].resize(object_names_.size()*6);
    for(size_t i = 0; i < dependent_sampling_blocks[0].size(); i++)
        dependent_sampling_blocks[0][i] = i;
    filter_->SamplingBlocks(dependent_sampling_blocks);

    if(state_is_partial)
    {
        // create the multi body initial samples ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        ff::FreeFloatingRigidBodiesState<> default_state(object_names_.size());
        for(size_t object_index = 0; object_index < object_names_.size(); object_index++)
            default_state.position(object_index) = Eigen::Vector3d(0, 0, 1.5); // outside of image

        std::vector<ff::FreeFloatingRigidBodiesState<> > multi_body_samples(initial_states.size());
        for(size_t state_index = 0; state_index < multi_body_samples.size(); state_index++)
            multi_body_samples[state_index] = default_state;

        std::cout << "doing evaluations " << std::endl;
        for(size_t body_index = 0; body_index < object_names_.size(); body_index++)
        {
            std::cout << "evalution of object " << object_names_[body_index] << std::endl;
            for(size_t state_index = 0; state_index < multi_body_samples.size(); state_index++)
            {
                ff::FreeFloatingRigidBodiesState<> full_initial_state(multi_body_samples[state_index]);
                full_initial_state[body_index] = initial_states[state_index];
                multi_body_samples[state_index] = full_initial_state;
            }
            filter_->Samples(multi_body_samples);
            filter_->Filter(image, ProcessModel::Input::Zero(object_names_.size()*6));
            filter_->Resample(multi_body_samples.size());

            multi_body_samples = filter_->Samples();
        }
    }
    else
    {
        std::vector<ff::FreeFloatingRigidBodiesState<> > multi_body_samples(initial_states.size());
        for(size_t i = 0; i < multi_body_samples.size(); i++)
            multi_body_samples[i] = initial_states[i];

        filter_->Samples(multi_body_samples);
        filter_->Filter(image, ProcessModel::Input::Zero(object_names_.size()*6));
   }
    filter_->Resample(evaluation_count/sampling_blocks.size());
    filter_->SamplingBlocks(sampling_blocks);

    shifting_average = filter_->StateDistribution().max();
}

Eigen::VectorXd MultiObjectTracker::Filter(const sensor_msgs::Image& ros_image)
{
    boost::mutex::scoped_lock lock(mutex_);

    if(std::isnan(last_measurement_time_))
        last_measurement_time_ = ros_image.header.stamp.toSec();
    Scalar delta_time = ros_image.header.stamp.toSec() - last_measurement_time_;


    std::cout << "actual delta time " << delta_time << std::endl;
    // convert image
    Observation image = ri::Ros2Eigen<Scalar>(ros_image, downsampling_factor_);

    // filter
    INIT_PROFILING;
    filter_->Filter(image, ProcessModel::Input::Zero(object_names_.size()*6));
    MEASURE("-----------------> total time for filtering");



    // visualize the mean state
    ff::FreeFloatingRigidBodiesState<> max = filter_->StateDistribution().max();


    // doing orientation average properly
    Eigen::Vector4d average_q = shifting_average.quaternion().coeffs();

    Eigen::Vector4d max_q = max.quaternion().coeffs();

    if(average_q.dot(max_q) < 0)
        max_q = -max_q;

    Eigen::Quaterniond q;
    q.coeffs() = (1.0-update_rate_) * average_q + update_rate_ * max_q;
    q.normalize();

    // taking weighted average
    shifting_average = (1.0-update_rate_) * shifting_average + update_rate_ * max;

    shifting_average.quaternion(q);


    for(size_t i = 0; i < object_names_.size(); i++)
    {
        std::string object_model_path =
                "package://dbot_ros_pkg/object_models/" + object_names_[i] + ".obj";
        ri::PublishMarker(shifting_average.homogeneous_matrix(i).cast<float>(),
                          ros_image.header, object_model_path, object_marker_publisher_,
                          i, 1, 0, 0);

	ri::PublishObjectState(shifting_average.homogeneous_matrix(i).cast<float>(),
			       ros_image.header, object_names_[i], object_state_publisher_);
    }

    last_measurement_time_ = ros_image.header.stamp.toSec();
    return shifting_average;
}