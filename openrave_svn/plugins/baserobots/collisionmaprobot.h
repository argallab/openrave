// Copyright (C) 2006-2010 Carnegie Mellon University (rdiankov@cs.cmu.edu)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef RAVE_COLLISIONMAP_ROBOT_H
#define RAVE_COLLISIONMAP_ROBOT_H

class CollisionMapRobot : public RobotBase
{
 public:
    class XMLData : public XMLReadable
    {
    public:
        /// specifies the free space of two joints
        template <int N>
        struct COLLISIONMAP
        {
            boost::multi_array<uint8_t,N> vfreespace; // 1 for free space, 0 for collision
            boost::array<dReal,N> fmin, fmax, fidelta;
            boost::array<string,N> jointnames;
            boost::array<int,N> jointindices;
        };
        typedef COLLISIONMAP<2> COLLISIONPAIR;
    XMLData() : XMLReadable("collisionmap") {}
        list<COLLISIONPAIR> listmaps;
    };

    class CollisionMapXMLReader : public BaseXMLReader
    {
    public:
        CollisionMapXMLReader(boost::shared_ptr<XMLData> cmdata, const AttributesList& atts) {
            _cmdata = cmdata;
            if( !_cmdata )
                _cmdata.reset(new XMLData());
        }

        virtual XMLReadablePtr GetReadable() { return _cmdata; }
        
        virtual ProcessElement startElement(const std::string& name, const AttributesList& atts) {
            _ss.str(""); // have to clear the string
            if( name == "pair" ) {
                _cmdata->listmaps.push_back(XMLData::COLLISIONPAIR());
                XMLData::COLLISIONPAIR& pair = _cmdata->listmaps.back();
                for(AttributesList::const_iterator itatt = atts.begin(); itatt != atts.end(); ++itatt) {
                    if( itatt->first == "dims" ) {
                        boost::array<size_t,2> dims={{0,0}};
                        stringstream ss(itatt->second);
                        ss >> dims[0] >> dims[1];
                        pair.vfreespace.resize(dims);
                    }
                    else if( itatt->first == "min" ) {
                        stringstream ss(itatt->second);
                        ss >> pair.fmin[0] >> pair.fmin[1];
                    }
                    else if( itatt->first == "max" ) {
                        stringstream ss(itatt->second);
                        ss >> pair.fmax[0] >> pair.fmax[1];
                    }
                    else if( itatt->first == "joints") {
                        stringstream ss(itatt->second);
                        ss >> pair.jointnames[0] >> pair.jointnames[1];
                    }
                }
                RAVELOG_VERBOSE(str(boost::format("creating self-collision pair: %s %s\n")%pair.jointnames[0]%pair.jointnames[1]));
                return PE_Support;
            }

            return PE_Pass;
        }

        virtual bool endElement(const std::string& name)
        {
            if( name == "pair" ) {
                BOOST_ASSERT(_cmdata->listmaps.size()>0);
                XMLData::COLLISIONPAIR& pair = _cmdata->listmaps.back();
                for(size_t i = 0; i < pair.vfreespace.shape()[0]; i++) {
                    for(size_t j = 0; j < pair.vfreespace.shape()[1]; j++) {
                        // have to read with an int, uint8_t gives bugs!
                        int freespace;
                        _ss >> freespace;
                        pair.vfreespace[i][j] = freespace;
                    }
                }
                if( !_ss )
                    RAVELOG_WARN("failed to read collision pair values\n");
            }
            else if( name == "collisionmap" )
                return true;
            else
                RAVELOG_ERROR("unknown field %s\n", name.c_str());
            return false;
        }

        virtual void characters(const std::string& ch)
        {
            _ss.clear();
            _ss << ch;
        }

    protected:
        boost::shared_ptr<XMLData> _cmdata;
        stringstream _ss;
    };

    static BaseXMLReaderPtr CreateXMLReader(InterfaceBasePtr ptr, const AttributesList& atts)
    {
        // ptr is the robot interface that this reader is being created for
        return BaseXMLReaderPtr(new CollisionMapXMLReader(boost::shared_ptr<XMLData>(),atts));
    }

    CollisionMapRobot(EnvironmentBasePtr penv) : RobotBase(penv) {
        __description = ":Interface Author: Rosen Diankov\n\nAllows user to specify regions of the robot configuration space that are in self-collision via lookup tables. This is done by the <collisionmap> XML tag.";
    }
    virtual ~CollisionMapRobot() {}

    virtual bool SetController(ControllerBasePtr controller, const std::vector<int>& jointindices, int nControlTransformation)
    {
        _pController = controller;
        if( !!_pController ) {
            if( !_pController->Init(shared_robot(),jointindices,nControlTransformation) ) {
                RAVELOG_WARN(str(boost::format("GenericRobot %s: Failed to init controller %s\n")%GetName()%controller->GetXMLId()));
                _pController.reset();
                return false;
            }
        }
        return true;
    }

    virtual bool SetMotion(TrajectoryBaseConstPtr ptraj)
    {
        BOOST_ASSERT(ptraj->GetPoints().size() > 0 || !"trajectory has no points\n");
        BOOST_ASSERT(ptraj->GetDOF() == GetDOF() || !"trajectory of wrong dimension");
        _trajcur = ptraj;
        return _pController->SetPath(_trajcur);
    }
 
    virtual bool SetActiveMotion(TrajectoryBaseConstPtr ptraj)
    {
        BOOST_ASSERT(ptraj->GetPoints().size() > 0 || !"trajectory has no points\n");
        BOOST_ASSERT(ptraj->GetDOF() == GetActiveDOF() || !"trajectory of wrong dimension");
        TrajectoryBasePtr pfulltraj = RaveCreateTrajectory(GetEnv(),ptraj->GetDOF());
        GetFullTrajectoryFromActive(pfulltraj, ptraj);
        _trajcur = pfulltraj;
        return _pController->SetPath(_trajcur);
    }

    virtual ControllerBasePtr GetController() const { return _pController; }
    virtual void SimulationStep(dReal fElapsedTime)
    {
        RobotBase::SimulationStep(fElapsedTime);
        if( !!_pController ) {
            _pController->SimulationStep(fElapsedTime);
        }
    }

    virtual void _ComputeInternalInformation()
    {
        RobotBase::_ComputeInternalInformation();
        boost::shared_ptr<XMLData> cmdata = boost::dynamic_pointer_cast<XMLData>(GetReadableInterface("collisionmap"));
        if( !!cmdata ) {
            // process the collisionmap structures
            FOREACH(itmap,cmdata->listmaps) {
                for(size_t i = 0; i < itmap->jointnames.size(); ++i) {
                    JointPtr pjoint = GetJoint(itmap->jointnames[i]);
                    itmap->fidelta.at(i) = (dReal)itmap->vfreespace.shape()[i]/(itmap->fmax.at(i)-itmap->fmin.at(i));
                    if( !pjoint ) {
                        itmap->jointindices.at(i) = -1;
                        RAVELOG_WARN(str(boost::format("failed to find joint %s specified in collisionmap\n")%itmap->jointnames[i]));
                    }
                    else
                        itmap->jointindices.at(i) = pjoint->GetJointIndex();
                }
            }
        }
    }

    virtual bool CheckSelfCollision(CollisionReportPtr report = CollisionReportPtr()) const
    {
        if( RobotBase::CheckSelfCollision(report) )
            return true;

        // check if the current joint angles fall within the allowable range
        boost::shared_ptr<XMLData> cmdata = boost::dynamic_pointer_cast<XMLData>(GetReadableInterface("collisionmap"));
        if( !!cmdata ) {
            vector<dReal> values;
            boost::array<int,2> indices={{0,0}};
            FOREACHC(itmap,cmdata->listmaps) {
                size_t i=0;
                const XMLData::COLLISIONPAIR& curmap = *itmap; // for debugging
                FOREACHC(itjindex,curmap.jointindices) {
                    if( *itjindex < 0 )
                        break;
                    GetJoints().at(*itjindex)->GetValues(values);
                    if( curmap.fmin[i] < curmap.fmax[i] ) {
                        int index = (int)((values.at(0)-curmap.fmin[i])*curmap.fidelta[i]);
                        if( index < 0 || index >= (int)curmap.vfreespace.shape()[i] )
                            break;
                        indices.at(i) = index;
                    }
                    ++i;
                }
                if( i != curmap.jointindices.size() )
                    continue;
                if( !curmap.vfreespace(indices) ) {
                    // get all colliding links and check to make sure that at least two are enabled
                    vector<LinkConstPtr> vLinkColliding;
                    FOREACHC(itjindex,curmap.jointindices) {
                        JointPtr pjoint = GetJoints().at(*itjindex);
                        if( !!pjoint->GetFirstAttached() && find(vLinkColliding.begin(),vLinkColliding.end(),pjoint->GetFirstAttached())== vLinkColliding.end() )
                            vLinkColliding.push_back(KinBody::LinkConstPtr(pjoint->GetFirstAttached()));
                        if( !!pjoint->GetSecondAttached() && find(vLinkColliding.begin(),vLinkColliding.end(),pjoint->GetSecondAttached())== vLinkColliding.end() )
                            vLinkColliding.push_back(KinBody::LinkConstPtr(pjoint->GetSecondAttached()));
                    }
                    int numenabled = 0;
                    FOREACHC(itlink,vLinkColliding) {
                        if( (*itlink)->IsEnabled() )
                            numenabled++;
                    }
                    if( numenabled < 2 )
                        continue;
                    if( !!report ) {
                        report->numCols = 1;
                        report->vLinkColliding = vLinkColliding;
                        if( vLinkColliding.size() > 0 )
                            report->plink1 = vLinkColliding.at(0);
                        if( vLinkColliding.size() > 1 )
                            report->plink2 = vLinkColliding.at(1);
                    }
                    RAVELOG_VERBOSE(str(boost::format("Self collision: joints %s(%d):%s(%d)\n")%curmap.jointnames[0]%indices[0]%curmap.jointnames[1]%indices[1]));
                    return true;
                }
            }
        }
        return false;
    }
    
 protected:
    TrajectoryBaseConstPtr _trajcur;
    ControllerBasePtr _pController;
};

#endif
