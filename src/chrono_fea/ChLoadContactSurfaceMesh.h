//
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2013 Project Chrono
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the top level of the distribution
// and at http://projectchrono.org/license-chrono.txt.
//
// File authors: Alessandro Tasora

#ifndef CHLOADCONTACTSURFACEMESH_H
#define CHLOADCONTACTSURFACEMESH_H

#include "chrono_fea/ChContactSurfaceMesh.h"
#include "physics/ChLoad.h"


namespace chrono {

namespace fea {



/// FORCE AT XYZ NODE
/// Loader for a constant force applied at a XYZ node.
/// Note that an equivalent shortcut is to set directly the node's own force member data
/// as mynode->SetForce(), but this other approach is more object-oriented (es. you can 
/// apply multiple forces at a single node, etc)

class ChLoaderXYZnode : public ChLoaderUVWatomic {
private:
    ChVector<> force;
public:
        // Useful: a constructor that also sets ChLoadable    
        ChLoaderXYZnode(ChSharedPtr<ChLoadableUVW> mloadable) 
            :  ChLoaderUVWatomic(mloadable,0,0,0) {
            this->force = VNULL;
        };

        // Compute F=F(u,v,w)
        // Implement it from base class.
        virtual void ComputeF(const double U,     ///< parametric coordinate -not used
                              const double V,     ///< parametric coordinate -not used
                              const double W,     ///< parametric coordinate -not used
                        ChVectorDynamic<>& F,       ///< Result F vector here, size must be = n.field coords.of loadable
                        ChVectorDynamic<>* state_x, ///< if != 0, update state (pos. part) to this, then evaluate F
                        ChVectorDynamic<>* state_w  ///< if != 0, update state (speed part) to this, then evaluate F
                        ) {
            F.PasteVector( this->force, 0,0);    // load, force part
        }

            /// Set force (ex. in [N] units), assumed to be constant in space and time,
            /// assumed applyed at the node.
        void SetForce(const ChVector<>& mf) {this->force = mf;}
        ChVector<> GetForce() const {return this->force;}
};


/// FORCE AT XYZ NODE (ready to use load)

class ChLoadXYZnode : public ChLoad<ChLoaderXYZnode> {
public:
    ChLoadXYZnode(ChSharedPtr<ChLoadableU> mloadable) 
        : ChLoad<ChLoaderXYZnode>(mloadable)
    {}
};



/// Class for applyings loads to a contact mesh as a cluster of forces
/// operating on the nodes of the underlying finite elements.
/// It is useful for doing cosimulation: one can pass this object's vertex & faces
/// to an external software (ex. CFD) that in turn will perform collision detection 
/// with its entities, compute forces, send forces back to Chrono via this object.

class ChApiFea ChLoadContactSurfaceMesh : public ChLoadBase {

    // Chrono simulation of RTTI, needed for serialization
    CH_RTTI(ChLoadContactSurfaceMesh, ChLoadBase);


  public:
    ChLoadContactSurfaceMesh(ChSharedPtr<ChContactSurfaceMesh> cmesh) {
        contactmesh = cmesh;
    };

    virtual ~ChLoadContactSurfaceMesh(){};


    // 
    // FUNCTIONS
    //

        // Functions that can be used
        // for cosimulation  A <----> B

        /// A --> B
        /// Get the collision mesh in a pointer-less way, where vertex are
        /// given in a vector of xyz points, and triangles are given as indexes to the three 
        /// vertexes in that vector. Similarly to Wavefront .OBJ meshes. Note, indexes are 0-based.
        /// These vectors can be later sent to another computing node that computes, say, CFD forces on the mesh.
    void OutputSimpleMesh(std::vector< ChVector<> >& vert_pos,    ///< array of vertexes (absolute xyz positions)
                          std::vector< ChVector<> >& vert_vel,    ///< array of vertexes (absolute xyz velocities, might be useful)
                          std::vector< ChVector<int> >& triangles ///< array of triangles (indexes to vertexes, ccw)
                            ) { 
        vert_pos.clear();
        vert_vel.clear();
        triangles.clear();
        size_t vertex_index = 0;
        auto trilist = this->contactmesh->GetTriangleList();
        // auxiliary map container to go from pointer-based mesh to index-based mesh:
        std::map<ChNodeFEAxyz*,size_t> ptr_ind_map;         
        for (size_t i = 0; i< trilist.size(); ++i) {
            if (!ptr_ind_map.count(trilist[i]->GetNode1().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode1().get_ptr(), vertex_index} );
                vert_pos.push_back(trilist[i]->GetNode1()->GetPos());
                vert_vel.push_back(trilist[i]->GetNode1()->GetPos_dt());
                ++vertex_index;
            }
            if (!ptr_ind_map.count(trilist[i]->GetNode2().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode2().get_ptr(), vertex_index} );
                vert_pos.push_back(trilist[i]->GetNode2()->GetPos());
                vert_vel.push_back(trilist[i]->GetNode2()->GetPos_dt());
                ++vertex_index;
            }
            if (!ptr_ind_map.count(trilist[i]->GetNode3().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode3().get_ptr(), vertex_index} );
                vert_pos.push_back(trilist[i]->GetNode3()->GetPos());
                vert_vel.push_back(trilist[i]->GetNode3()->GetPos_dt());
                ++vertex_index;
            }
        }
        for (size_t i = 0; i< trilist.size(); ++i) {
            triangles.push_back( ChVector<int> (
                                ptr_ind_map.at(trilist[i]->GetNode1().get_ptr()),
                                ptr_ind_map.at(trilist[i]->GetNode2().get_ptr()),
                                ptr_ind_map.at(trilist[i]->GetNode3().get_ptr()) ) );
        }
    }

        /// A <-- B
        /// Set the forces to the nodes in a pointer-less way, where forces are
        /// given as a vector of xyz vectors and indexes to the referenced vertex, as
        /// obtained by OutputSimpleMesh.
        /// NOTE! do not insert/remove nodes from the collision mesh 
        ///       between the OutputSimpleMesh-InputSimpleForces pair!
    void InputSimpleForces(const std::vector< ChVector<> > vert_forces,  ///< array of forces (absolute xyz forces in [N])
                           const std::vector< int > vert_ind          ///< array of indexes to vertexes to whom you apply forces
                            ) {
        // check the vert_forces and vert_ind arrays must have same size:
        assert (vert_forces.size() == vert_ind.size());
        // reset the previously applied forces if any:
        this->forces.clear();

        // prepare auxiliary map container to go from index-based mesh to pointer-based mesh:
        size_t vertex_index = 0;
        auto trilist = this->contactmesh->GetTriangleList();
        std::map<ChNodeFEAxyz*,size_t> ptr_ind_map; 
        std::vector<ChSharedPtr<ChNodeFEAxyz>> ind_ptr_map; 
        for (size_t i = 0; i< trilist.size(); ++i) {
            if (!ptr_ind_map.count(trilist[i]->GetNode1().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode1().get_ptr(), vertex_index} );
                ind_ptr_map.push_back(trilist[i]->GetNode1());
                ++vertex_index;
            }
            if (!ptr_ind_map.count(trilist[i]->GetNode2().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode2().get_ptr(), vertex_index} );
                ind_ptr_map.push_back(trilist[i]->GetNode1());
                ++vertex_index;
            }
            if (!ptr_ind_map.count(trilist[i]->GetNode3().get_ptr()) ) {
                ptr_ind_map.insert( {trilist[i]->GetNode3().get_ptr(), vertex_index} );
                ind_ptr_map.push_back(trilist[i]->GetNode1());
                ++vertex_index;
            }
        }
        // Populate the array of aplied loads to nodes
        for (size_t i = 0; i< vert_forces.size(); ++i) {
            ChSharedPtr< ChNodeFEAxyz > mnode = ind_ptr_map[vert_ind[i]];
            ChSharedPtr< ChLoadXYZnode > mforce (new ChLoadXYZnode(mnode));
            mforce->loader.SetForce( vert_forces[i] );
            this->forces.push_back(mforce);
        }
        
    }

        /// Set the contact mesh (also resets the applied nodes)
    void SetContactMesh(ChSharedPtr<ChContactSurfaceMesh> mmesh) {
        this->contactmesh = mmesh;
        this->forces.clear();
    }
        /// Get the contact mesh (also resets the applied nodes)
    ChSharedPtr<ChContactSurfaceMesh> GetContactMesh(ChSharedPtr<ChContactSurfaceMesh> mmesh) { return this->contactmesh;}

        /// Access the list of applied forces, so you can add new ones by using push_back(), 
        /// remove them, count them, etc.
        /// Note that if you add nodes, these should belong to the referenced mesh. 
    std::vector< ChSharedPtr<ChLoadXYZnode> >& GetForceList() {return forces;}


    //
    // ChLoadBase interface
    //

    virtual int LoadGet_ndof_x() { 
        int ndoftot = 0;
        for (int i= 0; i<forces.size(); ++i)
            ndoftot += forces[i]->LoadGet_ndof_x();
        return ndoftot;
    }
    virtual int LoadGet_ndof_w() { 
        int ndoftot = 0;
        for (int i= 0; i<forces.size(); ++i)
            ndoftot += forces[i]->LoadGet_ndof_w();
        return ndoftot;
    }
    virtual void LoadGetStateBlock_x(ChVectorDynamic<>& mD) { 
        int ndoftot = 0;
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->loader.GetLoadable()->LoadableGetStateBlock_x(ndoftot, mD);
            ndoftot += forces[i]->loader.GetLoadable()->LoadableGet_ndof_x();
        }
    }
    virtual void LoadGetStateBlock_w(ChVectorDynamic<>& mD) { 
        int ndoftot = 0;
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->loader.GetLoadable()->LoadableGetStateBlock_w(ndoftot, mD);
            ndoftot += forces[i]->loader.GetLoadable()->LoadableGet_ndof_w();
        }
    }
         // simple.. field is x y z, hardcoded return val:
    virtual int LoadGet_field_ncoords() { return 3;}

        /// Compute Q, the generalized load. Not needed.
    virtual void ComputeQ(ChState*      state_x, ///< state position to evaluate Q
                          ChStateDelta* state_w  ///< state speed to evaluate Q
                          ) {}

        /// Compute jacobians. 
        /// Not needed when forces are constant, btw.
    virtual void ComputeJacobian(ChState*      state_x, ///< state position to evaluate jacobians
                                 ChStateDelta* state_w, ///< state speed to evaluate jacobians
                                 ChMatrix<>& mK, ///< result dQ/dx
                                 ChMatrix<>& mR, ///< result dQ/dv
                                 ChMatrix<>& mM) ///< result dQ/da  
     {
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->ComputeJacobian(state_x, state_w, mK, mR, mM);
        }
     }; 

    virtual bool IsStiff() {return false;}

    virtual void CreateJacobianMatrices() {
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->CreateJacobianMatrices();
        }
    };

    virtual void LoadIntLoadResidual_F(ChVectorDynamic<>& R, const double c) {
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->LoadIntLoadResidual_F(R, c);
        }
    };

    virtual void InjectKRMmatrices(ChLcpSystemDescriptor& mdescriptor)  {
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->InjectKRMmatrices(mdescriptor);
        }
    } 

    virtual void KRMmatricesLoad(double Kfactor, double Rfactor, double Mfactor) {
        for (int i= 0; i<forces.size(); ++i) {
            forces[i]->KRMmatricesLoad(Kfactor, Rfactor, Mfactor);
        }
    }

 private:

    // 
    // DATA
    //
    ChSharedPtr<ChContactSurfaceMesh> contactmesh;

    std::vector< ChSharedPtr<ChLoadXYZnode> >  forces;
};



}  // END_OF_NAMESPACE____
}  // END_OF_NAMESPACE____

#endif