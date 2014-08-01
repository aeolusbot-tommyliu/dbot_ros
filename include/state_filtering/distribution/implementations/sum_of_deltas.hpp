/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014 Max-Planck-Institute for Intelligent Systems,
 *                     University of Southern California
 *    Manuel Wuthrich (manuel.wuthrich@gmail.com)
 *    Jan Issac (jan.issac@gmail.com)
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @date 05/25/2014
 * @author Manuel Wuthrich (manuel.wuthrich@gmail.com)
 * Max-Planck-Institute for Intelligent Systems, University of Southern California
 */

#ifndef STATE_FILTERING_DISTRIBUTION_IMPLEMENTATIONS_SUM_OF_DELTAS_HPP
#define STATE_FILTERING_DISTRIBUTION_IMPLEMENTATIONS_SUM_OF_DELTAS_HPP

// eigen
#include <Eigen/Dense>

// boost
#include <boost/assert.hpp>
#include <boost/utility/enable_if.hpp>

// std
#include <vector>

// state_filtering
#include <state_filtering/distribution/features/moments_solvable.hpp>

namespace filter
{


template <typename ScalarType_, int SIZE>
struct SumOfDeltasTypes
{
    typedef ScalarType_                           ScalarType;
    typedef Eigen::Matrix<ScalarType, SIZE, 1>    VectorType;
    typedef Eigen::Matrix<ScalarType, SIZE, SIZE> OperatorType;

    typedef MomentsSolvable<ScalarType, VectorType, OperatorType>   MomentsSolvableType;
};



// TODO: THIS DISTRIBUTION COULD BE GENERALIZED SUCH THAT IT CAN DEAL WITH
// ALL KINDS OF OBJECTS, NOT JUST EIGEN VECTORS
template <typename ScalarType_, int SIZE>
class SumOfDeltas: public SumOfDeltasTypes<ScalarType_, SIZE>::MomentsSolvableType
{
public:
    typedef SumOfDeltasTypes<ScalarType_, SIZE>::ScalarType      ScalarType;
    typedef SumOfDeltasTypes<ScalarType_, SIZE>::VectorType      VectorType;
    typedef SumOfDeltasTypes<ScalarType_, SIZE>::OperatorType    OperatorType;

    typedef typename std::vector<VectorType>   Deltas;
    typedef Eigen::Matrix<ScalarType, -1, 1>   Weights;

public:
    SumOfDeltas()
    {
        DISABLE_IF_DYNAMIC_SIZE(VectorType);

        // initialize with one delta at zero
        deltas_ = Deltas(1, VectorType::Zero());
        weights_ = Weights::Ones(1);
    }

    explicit SumOfDeltas(int variable_size)
    {
        DISABLE_IF_FIXED_SIZE(VectorType);

        // initialize with one delta at zero
        deltas_ = Deltas(1, VectorType::Zero(variable_size));
        weights_ = Weights::Ones(1);
    }

    virtual ~SumOfDeltas() { }

    virtual void setDeltas(const Deltas& deltas, const Weights& weights)
    {
        deltas_ = deltas;
        weights_ = weights.normalized();
    }

    virtual void setDeltas(const Deltas& deltas)
    {
        deltas_ = deltas;
        weights_ = Weights::Ones(deltas_.size())/ScalarType_(deltas_.size());
    }

    virtual void getDeltas(Deltas& deltas, Weights& weights) const
    {
        deltas = deltas_;
        weights = weights_;
    }

    virtual VectorType Mean() const
    {
        VectorType mean(VectorType::Zero(variable_size()));
        for(size_t i = 0; i < deltas_.size(); i++)
            mean += weights_[i] * deltas_[i];

        return mean;
    }

    virtual OperatorType Covariance() const
    {
        VectorType cached_mean = Mean();
        OperatorType covariance(OperatorType::Zero(variable_size(), variable_size()));
        for(size_t i = 0; i < deltas_.size(); i++)
            covariance += weights_[i] * (deltas_[i]-cached_mean) * (deltas_[i]-cached_mean).transpose();

        return covariance;
    }

    virtual int variable_size() const
    {
        return deltas_[0].rows();
    }

protected:
    Deltas  deltas_;
    Weights weights_;
};

}

#endif
