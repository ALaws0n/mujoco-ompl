// Wrap MuJoCo functionality to make it available to OMPL API

#pragma once

#include <iostream>
#include <mutex>
#include <vector>

#include <ompl/control/StatePropagator.h>
#include <ompl/control/SpaceInformation.h>

#include "mujoco.h"


struct JointInfo {
    std::string name;
    int type;
    bool limited;
    mjtNum range[2];
    int qposadr;
    int dofadr;
};

std::ostream& operator<<(std::ostream& os, const JointInfo& ji);

std::vector<JointInfo> getJointInfo(const mjModel* m);

struct StateRange {
    bool limited;
    mjtNum range[2];
};

StateRange getCtrlRange(const mjModel* m, size_t i);

struct MuJoCoState {
    mjtNum time;
    std::vector<mjtNum> qpos;
    std::vector<mjtNum> qvel;
    std::vector<mjtNum> act;
};

std::ostream& operator<<(std::ostream& os, const MuJoCoState& s);


// Put some sanity on the MuJoCo API
class MuJoCo {
  public:
    MuJoCo(std::string mjkey_filename):
      m(0), d(0) {
        // mj_activate and mj_deactivate should only be called
        // once per program
        mj_instance_count_lock.lock();
        if (mj_instance_count == 0) {
            mj_activate(mjkey_filename.c_str());
        }
        mj_instance_count += 1;
        mj_instance_count_lock.unlock();
    }

    ~MuJoCo() {
        if (d) mj_deleteData(d);
        if (m) mj_deleteModel(m);
        mj_instance_count_lock.lock();
        mj_instance_count -= 1;
        if (mj_instance_count == 0) {
            mj_deactivate();
        }
        mj_instance_count_lock.unlock();
    }

    // TODO: copy constructor
    // TODO: assignment operator

    bool loadXML(std::string filename) {
        if (m) mj_deleteModel(m);
        char error[1000];
        m = mj_loadXML(filename.c_str(), 0, error, 1000);
        if (!m) {
            std::cerr << error << std::endl;
        }
        return m;
    }

    bool makeData() {
        if (!m) {
            std::cerr << "Cannot makeData without a model!" << std::endl;
            return false;
        }
        if (d) mj_deleteData(d);
        d = mj_makeData(m);
        return d;
    }

    

    std::string getJointName(int i) const {
        // Avert your eyes of this horror
        return std::string(m->names + m->name_jntadr[i]);
    }

    std::string getBodyName(int i) const {
        return std::string(m->names + m->name_bodyadr[i]);
    }

    std::string getActName(int i) const {
        return std::string(m->names + m->name_actuatoradr[i]);
    }

    /// Set the world to random state within specified limits
    ///   modifies d->qpos and d->qvel
    void setRandomState() {
        mj_resetData(m, d);
        // Set default states
        for (size_t i=0; i < m->nq; i++) {
            d->qpos[i] = m->qpos0[i];
        }

        // Set random states within joint limit for DoFs
        auto joints = getJointInfo(m);
        for (size_t i=0; i < m->nv; i++) {
            int joint_id = m->dof_jntid[i];
            int qposadr = m->jnt_qposadr[ joint_id ];

            mjtNum r = ((mjtNum) rand()) / ((mjtNum) RAND_MAX);
            auto lower = joints[joint_id].range[0];
            auto upper = joints[joint_id].range[1];
            if (!joints[joint_id].limited) {
                // set range to -pi to pi
                lower = -3.1416;
                upper = 3.1416;
            }
            d->qpos[qposadr] = (r * (upper - lower)) + lower;

            // velocity = 0 seem reasonable
            d->qvel[i] = 0;
        }
    }

    MuJoCoState getState() const {
        MuJoCoState s;
        s.time = d->time;
        for (size_t i=0; i < m->nq; i++) {
            s.qpos.push_back(d->qpos[i]);
        }
        for (size_t i=0; i < m->nv; i++) {
            s.qvel.push_back(d->qvel[i]);
        }
        for (size_t i=0; i < m->na; i++) {
            s.act.push_back(d->act[i]);
        }
        return s;
    }

    void step() {
        mj_step(m, d);
    }

    mjModel* m;
    mjData* d;

  private:
    static int mj_instance_count;
    static std::mutex mj_instance_count_lock;
};



class MuJoCoStatePropagator : public ompl::control::StatePropagator {
  public:
    MuJoCoStatePropagator(
            std::shared_ptr<ompl::control::SpaceInformation> si,
            std::shared_ptr<MuJoCo> mj)
            : StatePropagator(si),
              mj(mj) {
    } 

    static std::shared_ptr<ompl::control::SpaceInformation>
    createSpaceInformation(const mjModel* m);

    const ompl::control::SpaceInformation* getSpaceInformation() const {
        return si_;
    }

    // To override this function from oc::StatePropagator, this has to be a
    // const function, but we need to modify the mjModel and mjData objects
    // to use MuJoCo to propagate a state
    // Use a preallocatd object, protect with mutex lock
    void propagate(
        const ompl::base::State* state,
        const ompl::control::Control* control,
        double duration,
        ompl::base::State* result) const override;

    bool canPropagateBackward() const override {
        return false;
    }

    bool canSteer() const override {
        return false;
    }

  private:
    mutable std::shared_ptr<MuJoCo> mj;
    mutable std::mutex mj_lock;
};

