/**
 * @file llviewerobject.cpp
 * @brief Base class for viewer objects
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llviewerobject.h"

#include "llaudioengine.h"
#include "indra_constants.h"
#include "llmath.h"
#include "llflexibleobject.h"
#include "llviewercontrol.h"
#include "lldatapacker.h"
#include "llfasttimer.h"
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "llframetimer.h"
#include "llhudicon.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "llmaterialtable.h"
#include "llmutelist.h"
#include "llnamevalue.h"
#include "llprimitive.h"
#include "llquantize.h"
#include "llregionhandle.h"
#include "llsdserialize.h"
#include "lltree_common.h"
#include "llxfermanager.h"
#include "message.h"
#include "object_flags.h"

#include "llaudiosourcevo.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llagentwearables.h"
#include "llbbox.h"
#include "llbox.h"
#include "llcylinder.h"
#include "llcontrolavatar.h"
#include "lldrawable.h"
#include "llface.h"
#include "llfloatertools.h"
#include "llfollowcam.h"
#include "llhudtext.h"
#include "llselectmgr.h"
#include "llrendersphere.h"
#include "lltooldraganddrop.h"
#include "lluiavatar.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "llviewerinventory.h"
#include "llviewerobjectlist.h"
#include "llviewerparceloverlay.h"
#include "llviewerpartsource.h"
#include "llviewerregion.h"
#include "llviewerstats.h"
#include "llviewertextureanim.h"
#include "llviewerwindow.h" // For getSpinAxis
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llvograss.h"
#include "llvosky.h"
#include "llvolume.h"
#include "llvolumemessage.h"
#include "llvopartgroup.h"
#include "llvosurfacepatch.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "llvowater.h"
#include "llworld.h"
#include "llui.h"
#include "pipeline.h"
#include "llviewernetwork.h"
#include "llvowlsky.h"
#include "llmanip.h"
#include "lltrans.h"
#include "llsdutil.h"
#include "llmediaentry.h"
#include "llfloaterperms.h"
#include "llvocache.h"
#include "llcleanup.h"
#include "llmeshrepository.h"
#include "llgltfmateriallist.h"
#include "llgl.h"
#include "gltf/asset.h"

//#define DEBUG_UPDATE_TYPE

bool        LLViewerObject::sVelocityInterpolate = true;
bool        LLViewerObject::sPingInterpolate = true;

U32         LLViewerObject::sNumZombieObjects = 0;
S32         LLViewerObject::sNumObjects = 0;
bool        LLViewerObject::sMapDebug = true;
LLColor4    LLViewerObject::sEditSelectColor(   1.0f, 1.f, 0.f, 0.3f);  // Edit OK
LLColor4    LLViewerObject::sNoEditSelectColor( 1.0f, 0.f, 0.f, 0.3f);  // Can't edit
S32         LLViewerObject::sAxisArrowLength(50);


bool        LLViewerObject::sPulseEnabled(false);
bool        LLViewerObject::sUseSharedDrawables(false); // true

// sMaxUpdateInterpolationTime must be greater than sPhaseOutUpdateInterpolationTime
F64Seconds  LLViewerObject::sMaxUpdateInterpolationTime(3.0);       // For motion interpolation: after X seconds with no updates, don't predict object motion
F64Seconds  LLViewerObject::sPhaseOutUpdateInterpolationTime(2.0);  // For motion interpolation: after Y seconds with no updates, taper off motion prediction
F64Seconds  LLViewerObject::sMaxRegionCrossingInterpolationTime(1.0);// For motion interpolation: don't interpolate over this time on region crossing

std::map<std::string, U32> LLViewerObject::sObjectDataMap;

// The maximum size of an object extra parameters binary (packed) block
#define MAX_OBJECT_PARAMS_SIZE 1024

// At 45 Hz collisions seem stable and objects seem
// to settle down at a reasonable rate.
// JC 3/18/2003

const F32 PHYSICS_TIMESTEP = 1.f / 45.f;
const U32 MAX_INV_FILE_READ_FAILS = 25;
const S32 MAX_OBJECT_BINARY_DATA_SIZE = 60 + 16;

const F64 INVENTORY_UPDATE_WAIT_TIME_DESYNC = 5; // seconds
const F64 INVENTORY_UPDATE_WAIT_TIME_OUTDATED = 1;

// static
LLViewerObject *LLViewerObject::createObject(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp, S32 flags)
{
    LL_PROFILE_ZONE_SCOPED;
    LL_DEBUGS("ObjectUpdate") << "creating " << id << LL_ENDL;

    LLViewerObject *res = NULL;

    if (gNonInteractive
        && pcode != LL_PCODE_LEGACY_AVATAR
        && pcode != LL_VO_SURFACE_PATCH
        && pcode != LL_VO_WATER
        && pcode != LL_VO_VOID_WATER
        && pcode != LL_VO_WL_SKY
        && pcode != LL_VO_SKY
        && pcode != LL_VO_PART_GROUP
        )
    {
        return res;
    }
    switch (pcode)
    {
    case LL_PCODE_VOLUME:
    {
        res = new LLVOVolume(id, pcode, regionp); break;
        break;
    }
    case LL_PCODE_LEGACY_AVATAR:
    {
        if (id == gAgentID)
        {
            if (!gAgentAvatarp)
            {
                gAgentAvatarp = new LLVOAvatarSelf(id, pcode, regionp);
                gAgentAvatarp->initInstance();
                gAgentWearables.setAvatarObject(gAgentAvatarp);
            }
            else
            {
                if (isAgentAvatarValid())
                {
                    gAgentAvatarp->updateRegion(regionp);
                }
            }
            res = gAgentAvatarp;
        }
        else if (flags & CO_FLAG_CONTROL_AVATAR)
        {
            LLControlAvatar *control_avatar = new LLControlAvatar(id, pcode, regionp);
            control_avatar->initInstance();
            res = control_avatar;
        }
        else if (flags & CO_FLAG_UI_AVATAR)
        {
            LLUIAvatar *ui_avatar = new LLUIAvatar(id, pcode, regionp);
            ui_avatar->initInstance();
            res = ui_avatar;
        }
        else
        {
            LLVOAvatar *avatar = new LLVOAvatar(id, pcode, regionp);
            avatar->initInstance();
            res = avatar;
        }
        break;
    }
    case LL_PCODE_LEGACY_GRASS:
      res = new LLVOGrass(id, pcode, regionp); break;
    case LL_PCODE_LEGACY_PART_SYS:
//    LL_WARNS() << "Creating old part sys!" << LL_ENDL;
//    res = new LLVOPart(id, pcode, regionp); break;
      res = NULL; break;
    case LL_PCODE_LEGACY_TREE:
      res = new LLVOTree(id, pcode, regionp); break;
    case LL_PCODE_TREE_NEW:
//    LL_WARNS() << "Creating new tree!" << LL_ENDL;
//    res = new LLVOTree(id, pcode, regionp); break;
      res = NULL; break;
    case LL_VO_SURFACE_PATCH:
      res = new LLVOSurfacePatch(id, pcode, regionp); break;
    case LL_VO_SKY:
      res = new LLVOSky(id, pcode, regionp); break;
    case LL_VO_VOID_WATER:
        res = new LLVOVoidWater(id, pcode, regionp); break;
    case LL_VO_WATER:
        res = new LLVOWater(id, pcode, regionp); break;
    case LL_VO_PART_GROUP:
      res = new LLVOPartGroup(id, pcode, regionp); break;
    case LL_VO_HUD_PART_GROUP:
      res = new LLVOHUDPartGroup(id, pcode, regionp); break;
    case LL_VO_WL_SKY:
      res = new LLVOWLSky(id, pcode, regionp); break;
    default:
      LL_WARNS() << "Unknown object pcode " << (S32)pcode << LL_ENDL;
      res = NULL; break;
    }

    return res;
}

LLViewerObject::LLViewerObject(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp, bool is_global)
:   LLPrimitive(),
    mChildList(),
    mID(id),
    mLocalID(0),
    mTotalCRC(0),
    mListIndex(-1),
    mTEImages(NULL),
    mTENormalMaps(NULL),
    mTESpecularMaps(NULL),
    mbCanSelect(true),
    mFlags(0),
    mPhysicsShapeType(0),
    mPhysicsGravity(0),
    mPhysicsFriction(0),
    mPhysicsDensity(0),
    mPhysicsRestitution(0),
    mDrawable(),
    mCreateSelected(false),
    mRenderMedia(false),
    mBestUpdatePrecision(0),
    mText(),
    mHudText(""),
    mHudTextColor(LLColor4::white),
    mControlAvatar(NULL),
    mLastInterpUpdateSecs(0.f),
    mLastMessageUpdateSecs(0.f),
    mLatestRecvPacketID(0),
    mRegionCrossExpire(0),
    mData(NULL),
    mAudioSourcep(NULL),
    mAudioGain(1.f),
    mSoundCutOffRadius(0.f),
    mAppAngle(0.f),
    mPixelArea(1024.f),
    mInventory(NULL),
    mInventorySerialNum(0),
    mExpectedInventorySerialNum(0),
    mInvRequestState(INVENTORY_REQUEST_STOPPED),
    mInvRequestXFerId(0),
    mInventoryDirty(false),
    mRegionp(regionp),
    mDead(false),
    mOrphaned(false),
    mUserSelected(false),
    mOnActiveList(false),
    mOnMap(false),
    mStatic(false),
    mSeatCount(0),
    mNumFaces(0),
    mRotTime(0.f),
    mAngularVelocityRot(),
    mPreviousRotation(),
    mAttachmentState(0),
    mMedia(NULL),
    mClickAction(0),
    mObjectCost(0),
    mLinksetCost(0),
    mPhysicsCost(0),
    mLinksetPhysicsCost(0.f),
    mCostStale(true),
    mPhysicsShapeUnknown(true),
    mAttachmentItemID(LLUUID::null),
    mLastUpdateType(OUT_UNKNOWN),
    mLastUpdateCached(false),
    mLocked(false),
    mCachedMuteListUpdateTime(0),
    mCachedOwnerInMuteList(false),
    mRiggedAttachedWarned(false)
{
    if (!is_global)
    {
        llassert(mRegionp);
    }

    LLPrimitive::init_primitive(pcode);

    // CP: added 12/2/2005 - this was being initialised to 0, not the current frame time
    mLastInterpUpdateSecs = LLFrameTimer::getElapsedSeconds();

    mPositionRegion = LLVector3(0.f, 0.f, 0.f);

    if (!is_global && mRegionp)
    {
        mPositionAgent = mRegionp->getOriginAgent();
    }
    resetRot();

    LLViewerObject::sNumObjects++;
}

LLViewerObject::~LLViewerObject()
{
    deleteTEImages();

    // unhook from reflection probe manager
    if (mReflectionProbe.notNull())
    {
        mReflectionProbe->mViewerObject = nullptr;
        mReflectionProbe = nullptr;
    }

    if(mInventory)
    {
        mInventory->clear();  // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }

    if (mPartSourcep)
    {
        mPartSourcep->setDead();
        mPartSourcep = NULL;
    }

    if (mText)
    {
        // something recovered LLHUDText when object was already dead
        mText->markDead();
        mText = NULL;
    }

    // Delete memory associated with extra parameters.
    std::unordered_map<U16, ExtraParameter*>::iterator iter;
    for (iter = mExtraParameterList.begin(); iter != mExtraParameterList.end(); ++iter)
    {
        if(iter->second != NULL)
        {
            delete iter->second->data;
            delete iter->second;
        }
    }
    mExtraParameterList.clear();

    for_each(mNameValuePairs.begin(), mNameValuePairs.end(), DeletePairedPointer()) ;
    mNameValuePairs.clear();

    delete[] mData;
    mData = NULL;

    delete mMedia;
    mMedia = NULL;

    sNumObjects--;
    sNumZombieObjects--;
    llassert(mChildList.size() == 0);
    llassert(mControlAvatar.isNull()); // Should have been cleaned by now
    if (mControlAvatar.notNull())
    {
        mControlAvatar->markForDeath();
        mControlAvatar = NULL;
        LL_WARNS() << "Dead object owned a live control avatar" << LL_ENDL;
    }

    clearInventoryListeners();
}

void LLViewerObject::deleteTEImages()
{
    delete[] mTEImages;
    mTEImages = NULL;

    if (mTENormalMaps != NULL)
    {
        delete[] mTENormalMaps;
        mTENormalMaps = NULL;
    }

    if (mTESpecularMaps != NULL)
    {
        delete[] mTESpecularMaps;
        mTESpecularMaps = NULL;
    }
}

void LLViewerObject::markDead()
{
    if (!mDead)
    {
        LL_PROFILE_ZONE_SCOPED;
        //LL_INFOS() << "Marking self " << mLocalID << " as dead." << LL_ENDL;

        // Root object of this hierarchy unlinks itself.
        if (getParent())
        {
            ((LLViewerObject *)getParent())->removeChild(this);
        }
        LLUUID mesh_id;
        {
            LLVOAvatar *av = getAvatar();
            if (av && LLVOAvatar::getRiggedMeshID(this,mesh_id))
            {
                // This case is needed for indirectly attached mesh objects.
                av->updateAttachmentOverrides();
            }
        }
        if (getControlAvatar())
        {
            unlinkControlAvatar();
        }

        // Mark itself as dead
        mDead = true;
        if(mRegionp)
        {
            mRegionp->removeFromCreatedList(getLocalID());
        }
        gObjectList.cleanupReferences(this);

        LLViewerObject *childp;
        while (mChildList.size() > 0)
        {
            childp = mChildList.back();
            if (childp->getPCode() != LL_PCODE_LEGACY_AVATAR)
            {
                //LL_INFOS() << "Marking child " << childp->getLocalID() << " as dead." << LL_ENDL;
                childp->setParent(NULL); // LLViewerObject::markDead 1
                childp->markDead();
            }
            else
            {
                // make sure avatar is no longer parented,
                // so we can properly set it's position
                childp->setDrawableParent(NULL);
                ((LLVOAvatar*)childp)->getOffObject();
                childp->setParent(NULL); // LLViewerObject::markDead 2
            }
            mChildList.pop_back();
        }

        if (mDrawable.notNull())
        {
            // Drawables are reference counted, mark as dead, then nuke the pointer.
            mDrawable->markDead();
            mDrawable = NULL;
        }

        if (mText)
        {
            mText->markDead();
            mText = NULL;
        }

        if (mIcon)
        {
            mIcon->markDead();
            mIcon = NULL;
        }

        if (mPartSourcep)
        {
            mPartSourcep->setDead();
            mPartSourcep = NULL;
        }

        if (mAudioSourcep)
        {
            // Do some cleanup
            if (gAudiop)
            {
                gAudiop->cleanupAudioSource(mAudioSourcep);
            }
            mAudioSourcep = NULL;
        }

        if (flagAnimSource())
        {
            if (isAgentAvatarValid())
            {
                // stop motions associated with this object
                gAgentAvatarp->stopMotionFromSource(mID);
            }
        }

        if (flagCameraSource())
        {
            LLFollowCamMgr::getInstance()->removeFollowCamParams(mID);
        }

        if (mReflectionProbe.notNull())
        {
            mReflectionProbe->mViewerObject = nullptr;
            mReflectionProbe = nullptr;
        }

        sNumZombieObjects++;
    }
}

void LLViewerObject::dump() const
{
    LL_INFOS() << "Type: " << pCodeToString(mPrimitiveCode) << LL_ENDL;
    LL_INFOS() << "Drawable: " << (LLDrawable *)mDrawable << LL_ENDL;
    LL_INFOS() << "Update Age: " << LLFrameTimer::getElapsedSeconds() - mLastMessageUpdateSecs << LL_ENDL;

    LL_INFOS() << "Parent: " << getParent() << LL_ENDL;
    LL_INFOS() << "ID: " << mID << LL_ENDL;
    LL_INFOS() << "LocalID: " << mLocalID << LL_ENDL;
    LL_INFOS() << "PositionRegion: " << getPositionRegion() << LL_ENDL;
    LL_INFOS() << "PositionAgent: " << getPositionAgent() << LL_ENDL;
    LL_INFOS() << "PositionGlobal: " << getPositionGlobal() << LL_ENDL;
    LL_INFOS() << "Velocity: " << getVelocity() << LL_ENDL;
    if (mDrawable.notNull() &&
        mDrawable->getNumFaces() &&
        mDrawable->getFace(0))
    {
        LLFacePool *poolp = mDrawable->getFace(0)->getPool();
        if (poolp)
        {
            LL_INFOS() << "Pool: " << poolp << LL_ENDL;
            LL_INFOS() << "Pool reference count: " << poolp->mReferences.size() << LL_ENDL;
        }
    }
    //LL_INFOS() << "BoxTree Min: " << mDrawable->getBox()->getMin() << LL_ENDL;
    //LL_INFOS() << "BoxTree Max: " << mDrawable->getBox()->getMin() << LL_ENDL;
    /*
    LL_INFOS() << "Velocity: " << getVelocity() << LL_ENDL;
    LL_INFOS() << "AnyOwner: " << permAnyOwner() << " YouOwner: " << permYouOwner() << " Edit: " << mPermEdit << LL_ENDL;
    LL_INFOS() << "UsePhysics: " << flagUsePhysics() << " CanSelect " << mbCanSelect << " UserSelected " << mUserSelected << LL_ENDL;
    LL_INFOS() << "AppAngle: " << mAppAngle << LL_ENDL;
    LL_INFOS() << "PixelArea: " << mPixelArea << LL_ENDL;

    char buffer[1000];
    char *key;
    for (key = mNameValuePairs.getFirstKey(); key; key = mNameValuePairs.getNextKey() )
    {
        mNameValuePairs[key]->printNameValue(buffer);
        LL_INFOS() << buffer << LL_ENDL;
    }
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        LL_INFOS() << "  child " << child->getID() << LL_ENDL;
    }
    */
}

void LLViewerObject::printNameValuePairs() const
{
    for (name_value_map_t::const_iterator iter = mNameValuePairs.begin();
         iter != mNameValuePairs.end(); iter++)
    {
        LLNameValue* nv = iter->second;
        LL_INFOS() << nv->printNameValue() << LL_ENDL;
    }
}

void LLViewerObject::initVOClasses()
{
    // Initialized shared class stuff first.
    LLVOAvatar::initClass();
    LLVOTree::initClass();
    LL_INFOS() << "Viewer Object size: " << sizeof(LLViewerObject) << LL_ENDL;
    LLVOGrass::initClass();
    LLVOWater::initClass();
    LLVOVolume::initClass();

    initObjectDataMap();
}

void LLViewerObject::cleanupVOClasses()
{
    SUBSYSTEM_CLEANUP(LLVOGrass);
    SUBSYSTEM_CLEANUP(LLVOWater);
    SUBSYSTEM_CLEANUP(LLVOTree);
    SUBSYSTEM_CLEANUP(LLVOAvatar);
    SUBSYSTEM_CLEANUP(LLVOVolume);

    sObjectDataMap.clear();
}

//object data map for compressed && !OUT_TERSE_IMPROVED
//static
void LLViewerObject::initObjectDataMap()
{
    U32 count = 0;

    sObjectDataMap["ID"] = count; //full id //LLUUID
    count += sizeof(LLUUID);

    sObjectDataMap["LocalID"] = count; //U32
    count += sizeof(U32);

    sObjectDataMap["PCode"] = count;   //U8
    count += sizeof(U8);

    sObjectDataMap["State"] = count;   //U8
    count += sizeof(U8);

    sObjectDataMap["CRC"] = count;     //U32
    count += sizeof(U32);

    sObjectDataMap["Material"] = count; //U8
    count += sizeof(U8);

    sObjectDataMap["ClickAction"] = count; //U8
    count += sizeof(U8);

    sObjectDataMap["Scale"] = count; //LLVector3
    count += sizeof(LLVector3);

    sObjectDataMap["Pos"] = count;   //LLVector3
    count += sizeof(LLVector3);

    sObjectDataMap["Rot"] = count;    //LLVector3
    count += sizeof(LLVector3);

    sObjectDataMap["SpecialCode"] = count; //U32
    count += sizeof(U32);

    sObjectDataMap["Owner"] = count; //LLUUID
    count += sizeof(LLUUID);

    sObjectDataMap["Omega"] = count; //LLVector3, when SpecialCode & 0x80 is set
    count += sizeof(LLVector3);

    //ParentID is after Omega if there is Omega, otherwise is after Owner
    sObjectDataMap["ParentID"] = count;//U32, when SpecialCode & 0x20 is set
    count += sizeof(U32);

    //-------
    //The rest items are not included here
    //-------
}

//static
void LLViewerObject::unpackVector3(LLDataPackerBinaryBuffer* dp, LLVector3& value, std::string name)
{
    dp->shift(sObjectDataMap[name]);
    dp->unpackVector3(value, name.c_str());
    dp->reset();
}

//static
void LLViewerObject::unpackUUID(LLDataPackerBinaryBuffer* dp, LLUUID& value, std::string name)
{
    dp->shift(sObjectDataMap[name]);
    dp->unpackUUID(value, name.c_str());
    dp->reset();
}

//static
void LLViewerObject::unpackU32(LLDataPackerBinaryBuffer* dp, U32& value, std::string name)
{
    dp->shift(sObjectDataMap[name]);
    dp->unpackU32(value, name.c_str());
    dp->reset();
}

//static
void LLViewerObject::unpackU8(LLDataPackerBinaryBuffer* dp, U8& value, std::string name)
{
    dp->shift(sObjectDataMap[name]);
    dp->unpackU8(value, name.c_str());
    dp->reset();
}

//static
U32 LLViewerObject::unpackParentID(LLDataPackerBinaryBuffer* dp, U32& parent_id)
{
    dp->shift(sObjectDataMap["SpecialCode"]);
    U32 value;
    dp->unpackU32(value, "SpecialCode");

    parent_id = 0;
    if(value & 0x20)
    {
        S32 offset = sObjectDataMap["ParentID"];
        if(!(value & 0x80))
        {
            offset -= sizeof(LLVector3);
        }

        dp->shift(offset);
        dp->unpackU32(parent_id, "ParentID");
    }
    dp->reset();

    return parent_id;
}

// Replaces all name value pairs with data from \n delimited list
// Does not update server
void LLViewerObject::setNameValueList(const std::string& name_value_list)
{
    // Clear out the old
    for_each(mNameValuePairs.begin(), mNameValuePairs.end(), DeletePairedPointer()) ;
    mNameValuePairs.clear();

    // Bring in the new
    std::string::size_type length = name_value_list.length();
    std::string::size_type start = 0;
    while (start < length)
    {
        std::string::size_type end = name_value_list.find_first_of("\n", start);
        if (end == std::string::npos) end = length;
        if (end > start)
        {
            std::string tok = name_value_list.substr(start, end - start);
            addNVPair(tok);
        }
        start = end+1;
    }
}

bool LLViewerObject::isAnySelected() const
{
    bool any_selected = isSelected();
    for (child_list_t::const_iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        const LLViewerObject* child = *iter;
        any_selected = any_selected || child->isSelected();
    }
    return any_selected;
}

void LLViewerObject::setSelected(bool sel)
{
    mUserSelected = sel;
    resetRot();

    if (!sel)
    {
        setAllTESelected(false);
    }
}

// This method returns true if the object is over land owned by the
// agent.
bool LLViewerObject::isReturnable()
{
    if (isAttachment())
    {
        return false;
    }

    std::vector<LLBBox> boxes;
    boxes.push_back(LLBBox(getPositionRegion(), getRotationRegion(), getScale() * -0.5f, getScale() * 0.5f).getAxisAligned());
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        boxes.push_back( LLBBox(child->getPositionRegion(), child->getRotationRegion(), child->getScale() * -0.5f, child->getScale() * 0.5f).getAxisAligned());
    }

    bool result = (mRegionp && mRegionp->objectIsReturnable(getPositionRegion(), boxes)) ? 1 : 0;

    if ( !result )
    {
        //Get list of neighboring regions relative to this vo's region
        std::vector<LLViewerRegion*> uniqueRegions;
        mRegionp->getNeighboringRegions( uniqueRegions );

        //Build aabb's - for root and all children
        std::vector<PotentialReturnableObject> returnables;
        typedef std::vector<LLViewerRegion*>::iterator RegionIt;
        RegionIt regionStart = uniqueRegions.begin();
        RegionIt regionEnd   = uniqueRegions.end();

        for (; regionStart != regionEnd; ++regionStart )
        {
            LLViewerRegion* pTargetRegion = *regionStart;
            //Add the root vo as there may be no children and we still want
            //to test for any edge overlap
            buildReturnablesForChildrenVO( returnables, this, pTargetRegion );
            //Add it's children
            for (child_list_t::iterator iter = mChildList.begin();  iter != mChildList.end(); iter++)
            {
                LLViewerObject* pChild = *iter;
                buildReturnablesForChildrenVO( returnables, pChild, pTargetRegion );
            }
        }

        //TBD#Eventually create a region -> box list map
        typedef std::vector<PotentialReturnableObject>::iterator ReturnablesIt;
        ReturnablesIt retCurrentIt = returnables.begin();
        ReturnablesIt retEndIt = returnables.end();

        for ( ; retCurrentIt !=retEndIt; ++retCurrentIt )
        {
            boxes.clear();
            LLViewerRegion* pRegion = (*retCurrentIt).pRegion;
            boxes.push_back( (*retCurrentIt).box );
            bool retResult =    pRegion
                             && pRegion->childrenObjectReturnable( boxes )
                             && pRegion->canManageEstate();
            if ( retResult )
            {
                result = true;
                break;
            }
        }
    }
    return result;
}

void LLViewerObject::buildReturnablesForChildrenVO( std::vector<PotentialReturnableObject>& returnables, LLViewerObject* pChild, LLViewerRegion* pTargetRegion )
{
    if ( !pChild )
    {
        LL_ERRS()<<"child viewerobject is NULL "<<LL_ENDL;
    }

    constructAndAddReturnable( returnables, pChild, pTargetRegion );

    //We want to handle any children VO's as well
    for (child_list_t::iterator iter = pChild->mChildList.begin();  iter != pChild->mChildList.end(); iter++)
    {
        LLViewerObject* pChildofChild = *iter;
        buildReturnablesForChildrenVO( returnables, pChildofChild, pTargetRegion );
    }
}

void LLViewerObject::constructAndAddReturnable( std::vector<PotentialReturnableObject>& returnables, LLViewerObject* pChild, LLViewerRegion* pTargetRegion )
{

    LLVector3 targetRegionPos;
    targetRegionPos.setVec( pChild->getPositionGlobal() );

    LLBBox childBBox = LLBBox( targetRegionPos, pChild->getRotationRegion(), pChild->getScale() * -0.5f,
                                pChild->getScale() * 0.5f).getAxisAligned();

    LLVector3 edgeA = targetRegionPos + childBBox.getMinLocal();
    LLVector3 edgeB = targetRegionPos + childBBox.getMaxLocal();

    LLVector3d edgeAd, edgeBd;
    edgeAd.setVec(edgeA);
    edgeBd.setVec(edgeB);

    //Only add the box when either of the extents are in a neighboring region
    if ( pTargetRegion->pointInRegionGlobal( edgeAd ) || pTargetRegion->pointInRegionGlobal( edgeBd ) )
    {
        PotentialReturnableObject returnableObj;
        returnableObj.box       = childBBox;
        returnableObj.pRegion   = pTargetRegion;
        returnables.push_back( returnableObj );
    }
}

bool LLViewerObject::crossesParcelBounds()
{
    std::vector<LLBBox> boxes;
    boxes.push_back(LLBBox(getPositionRegion(), getRotationRegion(), getScale() * -0.5f, getScale() * 0.5f).getAxisAligned());
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        boxes.push_back(LLBBox(child->getPositionRegion(), child->getRotationRegion(), child->getScale() * -0.5f, child->getScale() * 0.5f).getAxisAligned());
    }

    return mRegionp && mRegionp->objectsCrossParcel(boxes);
}

bool LLViewerObject::setParent(LLViewerObject* parent)
{
    if(mParent != parent)
    {
        LLViewerObject* old_parent = (LLViewerObject*)mParent ;
        bool ret = LLPrimitive::setParent(parent);
        if(ret && old_parent && parent)
        {
            old_parent->removeChild(this) ;
        }
        return ret ;
    }

    return false ;
}

void LLViewerObject::addChild(LLViewerObject *childp)
{
    for (child_list_t::iterator i = mChildList.begin(); i != mChildList.end(); ++i)
    {
        if (*i == childp)
        {   //already has child
            return;
        }
    }

    if (!isAvatar())
    {
        // propagate selection properties
        childp->mbCanSelect = mbCanSelect;
    }

    if(childp->setParent(this))
    {
        mChildList.push_back(childp);
        childp->afterReparent();

        if (childp->isAvatar())
        {
            mSeatCount++;
        }
    }
}

void LLViewerObject::onReparent(LLViewerObject *old_parent, LLViewerObject *new_parent)
{
}

void LLViewerObject::afterReparent()
{
}

void LLViewerObject::removeChild(LLViewerObject *childp)
{
    for (child_list_t::iterator i = mChildList.begin(); i != mChildList.end(); ++i)
    {
        if (*i == childp)
        {
            if (!childp->isAvatar() && mDrawable.notNull() && mDrawable->isActive() && childp->mDrawable.notNull() && !isAvatar())
            {
                gPipeline.markRebuild(childp->mDrawable, LLDrawable::REBUILD_VOLUME);
            }

            mChildList.erase(i);

            if(childp->getParent() == this)
            {
                childp->setParent(NULL);
            }

            if (childp->isAvatar())
            {
                mSeatCount--;
            }
            break;
        }
    }

    if (childp->isSelected())
    {
        LLSelectMgr::getInstance()->deselectObjectAndFamily(childp);
        bool add_to_end = true;
        LLSelectMgr::getInstance()->selectObjectAndFamily(childp, add_to_end);
    }
}

void LLViewerObject::addThisAndAllChildren(std::vector<LLViewerObject*>& objects)
{
    objects.push_back(this);
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        if (!child->isAvatar())
        {
            child->addThisAndAllChildren(objects);
        }
    }
}

void LLViewerObject::addThisAndNonJointChildren(std::vector<LLViewerObject*>& objects)
{
    objects.push_back(this);
    // don't add any attachments when temporarily selecting avatar
    if (isAvatar())
    {
        return;
    }
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        if ( (!child->isAvatar()))
        {
            child->addThisAndNonJointChildren(objects);
        }
    }
}

bool LLViewerObject::isChild(LLViewerObject *childp) const
{
    for (child_list_t::const_iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* testchild = *iter;
        if (testchild == childp)
            return true;
    }
    return false;
}

// returns true if at least one avatar is sitting on this object
bool LLViewerObject::isSeat() const
{
    return mSeatCount > 0;
}

bool LLViewerObject::setDrawableParent(LLDrawable* parentp)
{
    if (mDrawable.isNull())
    {
        return false;
    }

    bool ret = mDrawable->mXform.setParent(parentp ? &parentp->mXform : NULL);
    if(!ret)
    {
        return false ;
    }
    LLDrawable* old_parent = mDrawable->mParent;
    mDrawable->mParent = parentp;

    if (parentp && mDrawable->isActive())
    {
        parentp->makeActive();
        parentp->setState(LLDrawable::ACTIVE_CHILD);
    }

    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_VOLUME);
    if( (old_parent != parentp && old_parent)
        || (parentp && parentp->isActive()))
    {
        // *TODO we should not be relying on setDrawable parent to call markMoved
        gPipeline.markMoved(mDrawable, false);
    }
    else if (!mDrawable->isAvatar())
    {
        mDrawable->updateXform(true);
        /*if (!mDrawable->getSpatialGroup())
        {
            mDrawable->movePartition();
        }*/
    }

    return ret;
}

// Show or hide particles, icon and HUD
void LLViewerObject::hideExtraDisplayItems( bool hidden )
{
    if( mPartSourcep.notNull() )
    {
        LLViewerPartSourceScript *partSourceScript = mPartSourcep.get();
        partSourceScript->setSuspended( hidden );
    }

    if( mText.notNull() )
    {
        LLHUDText *hudText = mText.get();
        hudText->setHidden( hidden );
    }

    if( mIcon.notNull() )
    {
        LLHUDIcon *hudIcon = mIcon.get();
        hudIcon->setHidden( hidden );
    }
}

U32 LLViewerObject::checkMediaURL(const std::string &media_url)
{
    U32 retval = (U32)0x0;
    if (!mMedia && !media_url.empty())
    {
        retval |= MEDIA_URL_ADDED;
        mMedia = new LLViewerObjectMedia;
        mMedia->mMediaURL = media_url;
        mMedia->mMediaType = LLViewerObject::MEDIA_SET;
        mMedia->mPassedWhitelist = false;
    }
    else if (mMedia)
    {
        if (media_url.empty())
        {
            retval |= MEDIA_URL_REMOVED;
            delete mMedia;
            mMedia = NULL;
        }
        else if (mMedia->mMediaURL != media_url) // <-- This is an optimization.  If they are equal don't bother with below's test.
        {
            /*if (! (LLTextureEntry::getAgentIDFromMediaVersionString(media_url) == gAgent.getID() &&
                   LLTextureEntry::getVersionFromMediaVersionString(media_url) ==
                        LLTextureEntry::getVersionFromMediaVersionString(mMedia->mMediaURL) + 1))
            */
            {
                // If the media URL is different and WE were not the one who
                // changed it, mark dirty.
                retval |= MEDIA_URL_UPDATED;
            }
            mMedia->mMediaURL = media_url;
            mMedia->mPassedWhitelist = false;
        }
    }
    return retval;
}

//extract spatial information from object update message
//return parent_id
//static
U32 LLViewerObject::extractSpatialExtents(LLDataPackerBinaryBuffer *dp, LLVector3& pos, LLVector3& scale, LLQuaternion& rot)
{
    U32 parent_id = 0;
    LLViewerObject::unpackParentID(dp, parent_id);

    LLViewerObject::unpackVector3(dp, scale, "Scale");
    LLViewerObject::unpackVector3(dp, pos, "Pos");

    LLVector3 vec;
    LLViewerObject::unpackVector3(dp, vec, "Rot");
    rot.unpackFromVector3(vec);

    return parent_id;
}

U32 LLViewerObject::processUpdateMessage(LLMessageSystem *mesgsys,
                     void **user_data,
                     U32 block_num,
                     const EObjectUpdateType update_type,
                     LLDataPacker *dp)
{
    LL_PROFILE_ZONE_SCOPED;
    LL_DEBUGS_ONCE("SceneLoadTiming") << "Received viewer object data" << LL_ENDL;

    LL_DEBUGS("ObjectUpdate") << " mesgsys " << mesgsys << " dp " << dp << " id " << getID() << " update_type " << (S32) update_type << LL_ENDL;

    // The new OBJECTDATA_FIELD_SIZE_124, OBJECTDATA_FIELD_SIZE_140, OBJECTDATA_FIELD_SIZE_80
    // and OBJECTDATA_FIELD_SIZE_64 lengths should be supported in the existing cases below.
    // Each case should start at the beginning of the buffer and extract all known
    // values, and ignore any unknown data at the end of the buffer.
    // This allows new data in the future without breaking current viewers.
    const S32 OBJECTDATA_FIELD_SIZE_140 = 140;  // Full precision avatar update for future extended data
    const S32 OBJECTDATA_FIELD_SIZE_124 = 124;  // Full precision object update for future extended data
    const S32 OBJECTDATA_FIELD_SIZE_76  =  76;  // Full precision avatar update
    const S32 OBJECTDATA_FIELD_SIZE_60  =  60;  // Full precision object update
    const S32 OBJECTDATA_FIELD_SIZE_80 =   80;  // Terse avatar update, 16 bit precision for future extended data
    const S32 OBJECTDATA_FIELD_SIZE_64  =  64;  // Terse object update, 16 bit precision for future extended data
    const S32 OBJECTDATA_FIELD_SIZE_48  =  48;  // Terse avatar update, 16 bit precision
    const S32 OBJECTDATA_FIELD_SIZE_32  =  32;  // Terse object update, 16 bit precision

    U32 retval = 0x0;

    // If region is removed from the list it is also deleted.
    if (!LLWorld::instance().isRegionListed(mRegionp))
    {
        LL_WARNS() << "Updating object in an invalid region" << LL_ENDL;
        return retval;
    }

    // Coordinates of objects on simulators are region-local.
    U64 region_handle = 0;

    if(mesgsys != NULL)
    {
        mesgsys->getU64Fast(_PREHASH_RegionData, _PREHASH_RegionHandle, region_handle);
        LLViewerRegion* regionp = LLWorld::getInstance()->getRegionFromHandle(region_handle);
        if(regionp != mRegionp && regionp && mRegionp)//region cross
        {
            //this is the redundant position and region update, but it is necessary in case the viewer misses the following
            //position and region update messages from sim.
            //this redundant update should not cause any problems.
            LLVector3 delta_pos =  mRegionp->getOriginAgent() - regionp->getOriginAgent();
            setPositionParent(getPosition() + delta_pos); //update to the new region position immediately.
            setRegion(regionp) ; //change the region.
        }
        else
        {
            if(regionp != mRegionp)
            {
                if(mRegionp)
                {
                    mRegionp->removeFromCreatedList(getLocalID());
                }
                if(regionp)
                {
                    regionp->addToCreatedList(getLocalID());
                }
            }
            mRegionp = regionp ;
        }
    }

    if (!mRegionp)
    {
        U32 x, y;
        from_region_handle(region_handle, &x, &y);

        LL_WARNS("UpdateFail") << "Object has invalid region " << x << ":" << y << "!" << LL_ENDL;
        return retval;
    }

    F32 time_dilation = 1.f;
    if(mesgsys != NULL)
    {
        U16 time_dilation16;
        mesgsys->getU16Fast(_PREHASH_RegionData, _PREHASH_TimeDilation, time_dilation16);
        time_dilation = ((F32) time_dilation16) / 65535.f;
        mRegionp->setTimeDilation(time_dilation);
    }

    // this will be used to determine if we've really changed position
    // Use getPosition, not getPositionRegion, since this is what we're comparing directly against.
    LLVector3 test_pos_parent = getPosition();

    // This needs to match the largest size below. See switch(length)
    U8  data[MAX_OBJECT_BINARY_DATA_SIZE];

#ifdef LL_BIG_ENDIAN
    U16 valswizzle[4];
#endif
    U16 *val;
    const F32 size = LLWorld::getInstance()->getRegionWidthInMeters();
    const F32 MAX_HEIGHT = LLWorld::getInstance()->getRegionMaxHeight();
    const F32 MIN_HEIGHT = LLWorld::getInstance()->getRegionMinHeight();
    S32 length = 0;
    S32 count = 0;
    S32 this_update_precision = 32;     // in bits

    // Temporaries, because we need to compare w/ previous to set dirty flags...
    LLVector3 new_pos_parent;
    LLVector3 new_vel;
    LLVector3 new_acc;
    LLVector3 new_angv;
    LLVector3 old_angv = getAngularVelocity();
    LLQuaternion new_rot;
    LLVector3 new_scale = getScale();

    U32 parent_id = 0;
    U8  material = 0;
    U8 click_action = 0;
    U32 crc = 0;

    bool old_special_hover_cursor = specialHoverCursor();

    LLViewerObject *cur_parentp = (LLViewerObject *)getParent();

    if (cur_parentp)
    {
        parent_id = cur_parentp->mLocalID;
    }

    if (!dp)
    {
        switch(update_type)
        {
        case OUT_FULL:
            {
#ifdef DEBUG_UPDATE_TYPE
                LL_INFOS() << "Full:" << getID() << LL_ENDL;
#endif
                //clear cost and linkset cost
                setObjectCostStale();
                if (isSelected() && gFloaterTools)
                {
                    gFloaterTools->dirty();
                }

                LLUUID audio_uuid;
                LLUUID owner_id;    // only valid if audio_uuid or particle system is not null
                F32    gain;
                F32    cutoff;
                U8     sound_flags;

                mesgsys->getU32Fast( _PREHASH_ObjectData, _PREHASH_CRC, crc, block_num);
                mesgsys->getU32Fast( _PREHASH_ObjectData, _PREHASH_ParentID, parent_id, block_num);
                mesgsys->getUUIDFast(_PREHASH_ObjectData, _PREHASH_Sound, audio_uuid, block_num );
                // HACK: Owner id only valid if non-null sound id or particle system
                mesgsys->getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, owner_id, block_num );
                mesgsys->getF32Fast( _PREHASH_ObjectData, _PREHASH_Gain, gain, block_num );
                mesgsys->getF32Fast(  _PREHASH_ObjectData, _PREHASH_Radius, cutoff, block_num );
                mesgsys->getU8Fast(  _PREHASH_ObjectData, _PREHASH_Flags, sound_flags, block_num );
                mesgsys->getU8Fast(  _PREHASH_ObjectData, _PREHASH_Material, material, block_num );
                mesgsys->getU8Fast(  _PREHASH_ObjectData, _PREHASH_ClickAction, click_action, block_num);
                mesgsys->getVector3Fast(_PREHASH_ObjectData, _PREHASH_Scale, new_scale, block_num );
                length = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_ObjectData);
                mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_ObjectData, data, length, block_num, MAX_OBJECT_BINARY_DATA_SIZE);
                length = llmin(length, MAX_OBJECT_BINARY_DATA_SIZE);  // getBinaryDataFast() safely fills the buffer to max_size

                mTotalCRC = crc;
                // Might need to update mSourceMuted here to properly pick up new radius
                mSoundCutOffRadius = cutoff;

                // Owner ID used for sound muting or particle system muting
                setAttachedSound(audio_uuid, owner_id, gain, sound_flags);

                U8 old_material = getMaterial();
                if (old_material != material)
                {
                    setMaterial(material);
                    if (mDrawable.notNull())
                    {
                        gPipeline.markMoved(mDrawable, false); // undamped
                    }
                }
                setClickAction(click_action);

                count = 0;
                LLVector4 collision_plane;

                switch(length)
                {
                case OBJECTDATA_FIELD_SIZE_140:
                case OBJECTDATA_FIELD_SIZE_76:
                    // pull out collision normal for avatar
                    htolememcpy(collision_plane.mV, &data[count], MVT_LLVector4, sizeof(LLVector4));
                    ((LLVOAvatar*)this)->setFootPlane(collision_plane);
                    count += sizeof(LLVector4);

                case OBJECTDATA_FIELD_SIZE_124:
                case OBJECTDATA_FIELD_SIZE_60:
                    this_update_precision = 32;
                    // this is a full precision update
                    // pos
                    htolememcpy(new_pos_parent.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
                    count += sizeof(LLVector3);
                    // vel
                    htolememcpy((void*)getVelocity().mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
                    count += sizeof(LLVector3);
                    // acc
                    htolememcpy((void*)getAcceleration().mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
                    count += sizeof(LLVector3);
                    // theta
                    {
                        LLVector3 vec;
                        htolememcpy(vec.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
                        new_rot.unpackFromVector3(vec);
                    }
                    count += sizeof(LLVector3);
                    // omega
                    htolememcpy((void*)new_angv.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
                    if (new_angv.isExactlyZero())
                    {
                        // reset rotation time
                        resetRot();
                    }
                    setAngularVelocity(new_angv);
                    count += sizeof(LLVector3);
#if LL_DARWIN
                    if (length == OBJECTDATA_FIELD_SIZE_76 ||
                        length == OBJECTDATA_FIELD_SIZE_140)
                    {
                        setAngularVelocity(LLVector3::zero);
                    }
#endif
                    break;

                // length values 48, 32 and 16 were once in viewer code but
                //   are never sent by the SL simulator
                default:
                    LL_WARNS("UpdateFail") << "Unexpected ObjectData buffer size " << length
                        << " for " << getID() << " with OUT_FULL message" << LL_ENDL;
                }

                ////////////////////////////////////////////////////
                //
                // Here we handle data specific to the full message.
                //

                U32 flags;
                mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, block_num);
                // clear all but local flags
                mFlags &= FLAGS_LOCAL;
                mFlags |= flags;

                U8 state;
                mesgsys->getU8Fast(_PREHASH_ObjectData, _PREHASH_State, state, block_num );
                mAttachmentState = state;

                // ...new objects that should come in selected need to be added to the selected list
                mCreateSelected = ((flags & FLAGS_CREATE_SELECTED) != 0);

                // Set all name value pairs
                S32 nv_size = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_NameValue);
                if (nv_size > 0)
                {
                    std::string name_value_list;
                    mesgsys->getStringFast(_PREHASH_ObjectData, _PREHASH_NameValue, name_value_list, block_num);
                    setNameValueList(name_value_list);
                }

                // Clear out any existing generic data
                if (mData)
                {
                    delete [] mData;
                    mData = NULL;
                }

                // Dec 2023 new generic data:
                //    Trees work as before, this field contains genome data
                //    Not a tree:  root objects send 1 byte with the number of
                //      total prims in the linkset
                //    If the generic data size is zero, then number of prims is 1
                //
                // Viewers should not check for specific data sizes exactly, but if
                // the field has data, process it from the start and ignore the remainder.

                // Check for appended generic data
                const S32 GENERIC_DATA_BUFFER_SIZE = 16;
                S32 data_size = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_Data);
                if (data_size > 0)
                {    // has generic data
                    if (getPCode() == LL_PCODE_LEGACY_TREE || getPCode() == LL_PCODE_TREE_NEW)
                    {
                        mData = new U8[data_size];
                        mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_Data, mData, data_size, block_num);
                        LL_DEBUGS("NewObjectData") << "Read " << data_size << " bytes tree genome data for " << getID() << ", pcode "
                                             << getPCodeString() << ", value " << (S32) mData[0] << LL_ENDL;
                    }
                    else
                    {   // Extract number of prims
                        U8 generic_data[GENERIC_DATA_BUFFER_SIZE];
                        mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_Data,
                            &generic_data[0], llmin(data_size, GENERIC_DATA_BUFFER_SIZE), block_num);
                        // This is sample code to extract the number of prims
                        //    Future viewers should use it for their own purposes
                        if (!isAvatar())
                        {
                            S32 num_prims = (S32) generic_data[0];
                            LL_DEBUGS("NewObjectData") << "Root prim " << getID() << " has "
                                << num_prims << " prims in linkset" << LL_ENDL;
                        }
                    }
                }

                S32 text_size = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_Text);
                if (text_size > 1)
                {
                    // Setup object text
                    if (!mText)
                    {
                        initHudText();
                    }

                    std::string temp_string;
                    mesgsys->getStringFast(_PREHASH_ObjectData, _PREHASH_Text, temp_string, block_num );

                    LLColor4U coloru;
                    mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_TextColor, coloru.mV, 4, block_num);

                    // alpha was flipped so that it zero encoded better
                    coloru.mV[3] = 255 - coloru.mV[3];

                    mText->setColor(LLColor4(coloru));
                    mText->setString(temp_string);

                    mHudText = temp_string;
                    mHudTextColor = LLColor4(coloru);

                    setChanged(MOVED | SILHOUETTE);
                }
                else
                {
                    if (mText.notNull())
                    {
                        mText->markDead();
                        mText = NULL;
                    }
                    mHudText.clear();
                }

                std::string media_url;
                mesgsys->getStringFast(_PREHASH_ObjectData, _PREHASH_MediaURL, media_url, block_num);
                retval |= checkMediaURL(media_url);

                //
                // Unpack particle system data
                //
                unpackParticleSource(block_num, owner_id);

                // Mark all extra parameters not used
                std::unordered_map<U16, ExtraParameter*>::iterator iter;
                for (iter = mExtraParameterList.begin(); iter != mExtraParameterList.end(); ++iter)
                {
                    iter->second->in_use = false;
                }

                // Unpack extra parameters
                S32 size = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_ExtraParams);
                if (size > 0)
                {
                    U8 *buffer = new(std::nothrow) U8[size];
                    if (!buffer)
                    {
                        LLError::LLUserWarningMsg::showOutOfMemory();
                        LL_ERRS() << "Bad memory allocation for buffer, size: " << size << LL_ENDL;
                    }
                    mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_ExtraParams, buffer, size, block_num);
                    LLDataPackerBinaryBuffer dp(buffer, size);

                    U8 num_parameters;
                    dp.unpackU8(num_parameters, "num_params");
                    U8 param_block[MAX_OBJECT_PARAMS_SIZE];
                    for (U8 param=0; param<num_parameters; ++param)
                    {
                        U16 param_type;
                        S32 param_size;
                        dp.unpackU16(param_type, "param_type");
                        dp.unpackBinaryData(param_block, param_size, "param_data");
                        //LL_INFOS() << "Param type: " << param_type << ", Size: " << param_size << LL_ENDL;
                        LLDataPackerBinaryBuffer dp2(param_block, param_size);
                        unpackParameterEntry(param_type, &dp2);
                    }
                    delete[] buffer;
                }

                for (iter = mExtraParameterList.begin(); iter != mExtraParameterList.end(); ++iter)
                {
                    if (!iter->second->in_use)
                    {
                        // Send an update message in case it was formerly in use
                        parameterChanged(iter->first, iter->second->data, false, false);
                    }
                }

                break;
            }

        case OUT_TERSE_IMPROVED:
            {
#ifdef DEBUG_UPDATE_TYPE
                LL_INFOS() << "TI:" << getID() << LL_ENDL;
#endif
                length = mesgsys->getSizeFast(_PREHASH_ObjectData, block_num, _PREHASH_ObjectData);
                mesgsys->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_ObjectData, data, length, block_num, MAX_OBJECT_BINARY_DATA_SIZE);
                length = llmin(length, MAX_OBJECT_BINARY_DATA_SIZE);    // getBinaryDataFast() safely fills the buffer to max_size
                count  = 0;
                LLVector4 collision_plane;

                switch(length)
                {
                    case OBJECTDATA_FIELD_SIZE_80:
                    case OBJECTDATA_FIELD_SIZE_48:
                    // pull out collision normal for avatar
                    htolememcpy(collision_plane.mV, &data[count], MVT_LLVector4, sizeof(LLVector4));
                    ((LLVOAvatar*)this)->setFootPlane(collision_plane);
                    count += sizeof(LLVector4);

                case OBJECTDATA_FIELD_SIZE_64:
                case OBJECTDATA_FIELD_SIZE_32:
                    // this is a terse 16 bit quantized update
                    this_update_precision = 16;
                    test_pos_parent.quantize16(-0.5f*size, 1.5f*size, MIN_HEIGHT, MAX_HEIGHT);

#ifdef LL_BIG_ENDIAN
                    htolememcpy(valswizzle, &data[count], MVT_U16Vec3, 6);
                    val = valswizzle;
#else
                    val = (U16 *) &data[count];
#endif
                    count += sizeof(U16)*3;
                    new_pos_parent.mV[VX] = U16_to_F32(val[VX], -0.5f*size, 1.5f*size);
                    new_pos_parent.mV[VY] = U16_to_F32(val[VY], -0.5f*size, 1.5f*size);
                    new_pos_parent.mV[VZ] = U16_to_F32(val[VZ], MIN_HEIGHT, MAX_HEIGHT);

#ifdef LL_BIG_ENDIAN
                    htolememcpy(valswizzle, &data[count], MVT_U16Vec3, 6);
                    val = valswizzle;
#else
                    val = (U16 *) &data[count];
#endif
                    count += sizeof(U16)*3;
                    setVelocity(U16_to_F32(val[VX], -size, size),
                                U16_to_F32(val[VY], -size, size),
                                U16_to_F32(val[VZ], -size, size));

#ifdef LL_BIG_ENDIAN
                    htolememcpy(valswizzle, &data[count], MVT_U16Vec3, 6);
                    val = valswizzle;
#else
                    val = (U16 *) &data[count];
#endif
                    count += sizeof(U16)*3;
                    setAcceleration(U16_to_F32(val[VX], -size, size),
                                    U16_to_F32(val[VY], -size, size),
                                    U16_to_F32(val[VZ], -size, size));

#ifdef LL_BIG_ENDIAN
                    htolememcpy(valswizzle, &data[count], MVT_U16Quat, 8);
                    val = valswizzle;
#else
                    val = (U16 *) &data[count];
#endif
                    count += sizeof(U16)*4;
                    new_rot.mQ[VX] = U16_to_F32(val[VX], -1.f, 1.f);
                    new_rot.mQ[VY] = U16_to_F32(val[VY], -1.f, 1.f);
                    new_rot.mQ[VZ] = U16_to_F32(val[VZ], -1.f, 1.f);
                    new_rot.mQ[VW] = U16_to_F32(val[VW], -1.f, 1.f);

#ifdef LL_BIG_ENDIAN
                    htolememcpy(valswizzle, &data[count], MVT_U16Vec3, 6);
                    val = valswizzle;
#else
                    val = (U16 *) &data[count];
#endif
                    new_angv.set(U16_to_F32(val[VX], -size, size),
                                        U16_to_F32(val[VY], -size, size),
                                        U16_to_F32(val[VZ], -size, size));
                    setAngularVelocity(new_angv);
                    break;

                // Previous viewers had code for length 76, 60 or 16 byte length
                // with full precision or 8 bit quanitzation, but the
                // SL servers will never send those data formats.  If you ever see this
                // warning in Second Life, please file a bug report
                default:
                    LL_WARNS("UpdateFail") << "Unexpected ObjectData buffer size " << length << " for " << getID()
                                           << " with OUT_FULL message" << LL_ENDL;
                }

                U8 state;
                mesgsys->getU8Fast(_PREHASH_ObjectData, _PREHASH_State, state, block_num );
                mAttachmentState = state;
                break;
            }

        default:
            LL_WARNS("UpdateFail") << "Unknown uncompressed update type " << update_type << " for " << getID() << LL_ENDL;
            break;
        }
    }
    else
    {
        // handle the compressed case - have dp datapacker
        LLUUID sound_uuid;
        LLUUID  owner_id;
        F32    gain = 0;
        U8     sound_flags = 0;
        F32     cutoff = 0;

        U16 val[4];

        U8      state;

        dp->unpackU8(state, "State");
        mAttachmentState = state;

        switch(update_type)
        {
            case OUT_TERSE_IMPROVED:
            {
#ifdef DEBUG_UPDATE_TYPE
                LL_INFOS() << "CompTI:" << getID() << LL_ENDL;
#endif
                U8      value;
                dp->unpackU8(value, "agent");
                if (value)
                {
                    LLVector4 collision_plane;
                    dp->unpackVector4(collision_plane, "Plane");
                    ((LLVOAvatar*)this)->setFootPlane(collision_plane);
                }
                test_pos_parent = getPosition();
                dp->unpackVector3(new_pos_parent, "Pos");
                dp->unpackU16(val[VX], "VelX");
                dp->unpackU16(val[VY], "VelY");
                dp->unpackU16(val[VZ], "VelZ");
                setVelocity(U16_to_F32(val[VX], -128.f, 128.f),
                            U16_to_F32(val[VY], -128.f, 128.f),
                            U16_to_F32(val[VZ], -128.f, 128.f));
                dp->unpackU16(val[VX], "AccX");
                dp->unpackU16(val[VY], "AccY");
                dp->unpackU16(val[VZ], "AccZ");
                setAcceleration(U16_to_F32(val[VX], -64.f, 64.f),
                                U16_to_F32(val[VY], -64.f, 64.f),
                                U16_to_F32(val[VZ], -64.f, 64.f));

                dp->unpackU16(val[VX], "ThetaX");
                dp->unpackU16(val[VY], "ThetaY");
                dp->unpackU16(val[VZ], "ThetaZ");
                dp->unpackU16(val[VS], "ThetaS");
                new_rot.mQ[VX] = U16_to_F32(val[VX], -1.f, 1.f);
                new_rot.mQ[VY] = U16_to_F32(val[VY], -1.f, 1.f);
                new_rot.mQ[VZ] = U16_to_F32(val[VZ], -1.f, 1.f);
                new_rot.mQ[VS] = U16_to_F32(val[VS], -1.f, 1.f);
                dp->unpackU16(val[VX], "AccX");
                dp->unpackU16(val[VY], "AccY");
                dp->unpackU16(val[VZ], "AccZ");
                new_angv.set(U16_to_F32(val[VX], -64.f, 64.f),
                                    U16_to_F32(val[VY], -64.f, 64.f),
                                    U16_to_F32(val[VZ], -64.f, 64.f));
                setAngularVelocity(new_angv);
            }
            break;
            case OUT_FULL_COMPRESSED:
            case OUT_FULL_CACHED:
            {
#ifdef DEBUG_UPDATE_TYPE
                LL_INFOS() << "CompFull:" << getID() << LL_ENDL;
#endif
                setObjectCostStale();

                if (isSelected() && gFloaterTools)
                {
                    gFloaterTools->dirty();
                }

                dp->unpackU32(crc, "CRC");
                mTotalCRC = crc;
                dp->unpackU8(material, "Material");
                U8 old_material = getMaterial();
                if (old_material != material)
                {
                    setMaterial(material);
                    if (mDrawable.notNull())
                    {
                        gPipeline.markMoved(mDrawable, false); // undamped
                    }
                }
                dp->unpackU8(click_action, "ClickAction");
                setClickAction(click_action);
                dp->unpackVector3(new_scale, "Scale");
                dp->unpackVector3(new_pos_parent, "Pos");
                LLVector3 vec;
                dp->unpackVector3(vec, "Rot");
                new_rot.unpackFromVector3(vec);
                setAcceleration(LLVector3::zero);

                U32 value;
                dp->unpackU32(value, "SpecialCode");
                dp->setPassFlags(value);
                dp->unpackUUID(owner_id, "Owner");

                mOwnerID = owner_id;

                if (value & 0x80)
                {
                    dp->unpackVector3(new_angv, "Omega");
                    setAngularVelocity(new_angv);
                }

                if (value & 0x20)
                {
                    dp->unpackU32(parent_id, "ParentID");
                }
                else
                {
                    parent_id = 0;
                }

                S32 sp_size;
                U32 size;
                if (value & 0x2)
                {
                    sp_size = 1;
                    delete [] mData;
                    mData = new U8[1];
                    dp->unpackU8(((U8*)mData)[0], "TreeData");
                }
                else if (value & 0x1)
                {
                    dp->unpackU32(size, "ScratchPadSize");
                    delete [] mData;
                    mData = new U8[size];
                    dp->unpackBinaryData((U8 *)mData, sp_size, "PartData");
                }
                else
                {
                    mData = NULL;
                }

                // Setup object text
                if (!mText && (value & 0x4))
                {
                    initHudText();
                }

                if (value & 0x4)
                {
                    std::string temp_string;
                    dp->unpackString(temp_string, "Text");

                    LLColor4U coloru;
                    dp->unpackBinaryDataFixed(coloru.mV, 4, "Color");
                    coloru.mV[3] = 255 - coloru.mV[3];
                    mText->setColor(LLColor4(coloru));
                    mText->setString(temp_string);

                    mHudText = temp_string;
                    mHudTextColor = LLColor4(coloru);

                    setChanged(TEXTURE);
                }
                else
                {
                    if (mText.notNull())
                    {
                        mText->markDead();
                        mText = NULL;
                    }
                    mHudText.clear();
                }

                std::string media_url;
                if (value & 0x200)
                {
                    dp->unpackString(media_url, "MediaURL");
                }
                retval |= checkMediaURL(media_url);

                //
                // Unpack particle system data (legacy)
                //
                if (value & 0x8)
                {
                    unpackParticleSource(*dp, owner_id, true);
                }
                else if (!(value & 0x400))
                {
                    deleteParticleSource();
                }

                // Mark all extra parameters not used
                std::unordered_map<U16, ExtraParameter*>::iterator iter;
                for (iter = mExtraParameterList.begin(); iter != mExtraParameterList.end(); ++iter)
                {
                    iter->second->in_use = false;
                }

                // Unpack extra params
                U8 num_parameters;
                dp->unpackU8(num_parameters, "num_params");
                U8 param_block[MAX_OBJECT_PARAMS_SIZE];
                for (U8 param=0; param<num_parameters; ++param)
                {
                    U16 param_type;
                    S32 param_size;
                    dp->unpackU16(param_type, "param_type");
                    dp->unpackBinaryData(param_block, param_size, "param_data");
                    //LL_INFOS() << "Param type: " << param_type << ", Size: " << param_size << LL_ENDL;
                    LLDataPackerBinaryBuffer dp2(param_block, param_size);
                    unpackParameterEntry(param_type, &dp2);
                }

                for (iter = mExtraParameterList.begin(); iter != mExtraParameterList.end(); ++iter)
                {
                    if (!iter->second->in_use)
                    {
                        // Send an update message in case it was formerly in use
                        parameterChanged(iter->first, iter->second->data, false, false);
                    }
                }

                if (value & 0x10)
                {
                    dp->unpackUUID(sound_uuid, "SoundUUID");
                    dp->unpackF32(gain, "SoundGain");
                    dp->unpackU8(sound_flags, "SoundFlags");
                    dp->unpackF32(cutoff, "SoundRadius");
                }

                if (value & 0x100)
                {
                    std::string name_value_list;
                    dp->unpackString(name_value_list, "NV");

                    setNameValueList(name_value_list);
                }

                mTotalCRC = crc;
                mSoundCutOffRadius = cutoff;

                setAttachedSound(sound_uuid, owner_id, gain, sound_flags);

                // only get these flags on updates from sim, not cached ones
                // Preload these five flags for every object.
                // Finer shades require the object to be selected, and the selection manager
                // stores the extended permission info.
                if(mesgsys != NULL)
                {
                U32 flags;
                mesgsys->getU32Fast(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, block_num);
                loadFlags(flags);
                }
            }
            break;

        default:
            LL_WARNS("UpdateFail") << "Unknown compressed update type " << update_type << " for " << getID() << LL_ENDL;
            break;
        }
    }

    //
    // Fix object parenting.
    //
    bool b_changed_status = false;

    if (OUT_TERSE_IMPROVED != update_type)
    {
        // We only need to update parenting on full updates, terse updates
        // don't send parenting information.
        if (!cur_parentp)
        {
            if (parent_id == 0)
            {
                // No parent now, no parent in message -> do nothing
            }
            else
            {
                // No parent now, new parent in message -> attach to that parent if possible
                LLUUID parent_uuid;

                if(mesgsys != NULL)
                {
                    gObjectList.getUUIDFromLocal(parent_uuid,
                                                        parent_id,
                                                        mesgsys->getSenderIP(),
                                                        mesgsys->getSenderPort());
                }
                else
                {
                    gObjectList.getUUIDFromLocal(parent_uuid,
                                                        parent_id,
                                                        mRegionp->getHost().getAddress(),
                                                        mRegionp->getHost().getPort());
                }

                LLViewerObject *sent_parentp = gObjectList.findObject(parent_uuid);

                //
                // Check to see if we have the corresponding viewer object for the parent.
                //
                if (sent_parentp && sent_parentp->getParent() == this)
                {
                    // Try to recover if we attempt to attach a parent to its child
                    LL_WARNS("UpdateFail") << "Attempt to attach a parent to it's child: " << this->getID() << " to "
                                           << sent_parentp->getID() << LL_ENDL;
                    this->removeChild(sent_parentp);
                    sent_parentp->setDrawableParent(NULL);
                }

                if (sent_parentp && (sent_parentp != this) && !sent_parentp->isDead())
                {
                    if (((LLViewerObject*)sent_parentp)->isAvatar())
                    {
                        //LL_DEBUGS("Avatar") << "ATT got object update for attachment " << LL_ENDL;
                    }

                    //
                    // We have a viewer object for the parent, and it's not dead.
                    // Do the actual reparenting here.
                    //

                    // new parent is valid
                    b_changed_status = true;
                    // ...no current parent, so don't try to remove child
                    if (mDrawable.notNull())
                    {
                        if (mDrawable->isDead() || !mDrawable->getVObj())
                        {
                            LL_WARNS("UpdateFail") << "Drawable is dead or no VObj!" << LL_ENDL;
                            sent_parentp->addChild(this);
                        }
                        else
                        {
                            if (!setDrawableParent(sent_parentp->mDrawable)) // LLViewerObject::processUpdateMessage 1
                            {
                                // Bad, we got a cycle somehow.
                                // Kill both the parent and the child, and
                                // set cache misses for both of them.
                                LL_WARNS("UpdateFail") << "Attempting to recover from parenting cycle!  "
                                            << "Killing " << sent_parentp->getID() << " and " << getID()
                                            << ", Adding to cache miss list" << LL_ENDL;
                                setParent(NULL);
                                sent_parentp->setParent(NULL);
                                getRegion()->addCacheMissFull(getLocalID());
                                getRegion()->addCacheMissFull(sent_parentp->getLocalID());
                                gObjectList.killObject(sent_parentp);
                                gObjectList.killObject(this);
                                return retval;
                            }
                            sent_parentp->addChild(this);
                            // make sure this object gets a non-damped update
                            if (sent_parentp->mDrawable.notNull())
                            {
                                gPipeline.markMoved(sent_parentp->mDrawable, false); // undamped
                            }
                        }
                    }
                    else
                    {
                        sent_parentp->addChild(this);
                    }

                    // Show particles, icon and HUD
                    hideExtraDisplayItems( false );

                    setChanged(MOVED | SILHOUETTE);
                }
                else
                {
                    //
                    // No corresponding viewer object for the parent, put the various
                    // pieces on the orphan list.
                    //

                    //parent_id
                    U32 ip, port;

                    if(mesgsys != NULL)
                    {
                        ip = mesgsys->getSenderIP();
                        port = mesgsys->getSenderPort();
                    }
                    else
                    {
                        ip = mRegionp->getHost().getAddress();
                        port = mRegionp->getHost().getPort();
                    }
                    gObjectList.orphanize(this, parent_id, ip, port);

                    // Hide particles, icon and HUD
                    hideExtraDisplayItems( true );
                }
            }
        }
        else
        {
            // BUG: this is a bad assumption once border crossing is alowed
            if (  (parent_id == cur_parentp->mLocalID)
                &&(update_type == OUT_TERSE_IMPROVED))
            {
                // Parent now, same parent in message -> do nothing

                // Debugging for suspected problems with local ids.
                //LLUUID parent_uuid;
                //gObjectList.getUUIDFromLocal(parent_uuid, parent_id, mesgsys->getSenderIP(), mesgsys->getSenderPort() );
                //if (parent_uuid != cur_parentp->getID() )
                //{
                //  LL_ERRS() << "Local ID match but UUID mismatch of viewer object" << LL_ENDL;
                //}
            }
            else
            {
                // Parented now, different parent in message
                LLViewerObject *sent_parentp;
                if (parent_id == 0)
                {
                    //
                    // This object is no longer parented, we sent in a zero parent ID.
                    //
                    sent_parentp = NULL;
                }
                else
                {
                    LLUUID parent_uuid;

                    if(mesgsys != NULL)
                    {
                        gObjectList.getUUIDFromLocal(parent_uuid,
                                                        parent_id,
                                                        gMessageSystem->getSenderIP(),
                                                        gMessageSystem->getSenderPort());
                    }
                    else
                    {
                        gObjectList.getUUIDFromLocal(parent_uuid,
                                                        parent_id,
                                                        mRegionp->getHost().getAddress(),
                                                        mRegionp->getHost().getPort());
                    }
                    sent_parentp = gObjectList.findObject(parent_uuid);

                    if (isAvatar())
                    {
                        // This logic is meant to handle the case where a sitting avatar has reached a new sim
                        // ahead of the object she was sitting on (which is common as objects are transfered through
                        // a slower route than agents)...
                        // In this case, the local id for the object will not be valid, since the viewer has not received
                        // a full update for the object from that sim yet, so we assume that the agent is still sitting
                        // where she was originally. --RN
                        if (!sent_parentp)
                        {
                            sent_parentp = cur_parentp;
                        }
                    }
                    else if (!sent_parentp)
                    {
                        //
                        // Switching parents, but we don't know the new parent.
                        //
                        U32 ip, port;

                        if(mesgsys != NULL)
                        {
                            ip = mesgsys->getSenderIP();
                            port = mesgsys->getSenderPort();
                        }
                        else
                        {
                            ip = mRegionp->getHost().getAddress();
                            port = mRegionp->getHost().getPort();
                        }

                        // We're an orphan, flag things appropriately.
                        gObjectList.orphanize(this, parent_id, ip, port);
                    }
                }

                // Reattach if possible.
                if (sent_parentp && sent_parentp != cur_parentp && sent_parentp != this)
                {
                    // New parent is valid, detach and reattach
                    b_changed_status = true;
                    if (mDrawable.notNull())
                    {
                        if (!setDrawableParent(sent_parentp->mDrawable)) // LLViewerObject::processUpdateMessage 2
                        {
                            // Bad, we got a cycle somehow.
                            // Kill both the parent and the child, and
                            // set cache misses for both of them.
                            LL_WARNS() << "Attempting to recover from parenting cycle!" << LL_ENDL;
                            LL_WARNS() << "Killing " << sent_parentp->getID() << " and " << getID() << LL_ENDL;
                            LL_WARNS() << "Adding to cache miss list" << LL_ENDL;
                            setParent(NULL);
                            sent_parentp->setParent(NULL);
                            getRegion()->addCacheMissFull(getLocalID());
                            getRegion()->addCacheMissFull(sent_parentp->getLocalID());
                            gObjectList.killObject(sent_parentp);
                            gObjectList.killObject(this);
                            return retval;
                        }
                        // make sure this object gets a non-damped update
                    }
                    cur_parentp->removeChild(this);
                    sent_parentp->addChild(this);
                    setChanged(MOVED | SILHOUETTE);
                    sent_parentp->setChanged(MOVED | SILHOUETTE);
                    if (sent_parentp->mDrawable.notNull())
                    {
                        gPipeline.markMoved(sent_parentp->mDrawable, false); // undamped
                    }
                }
                else if (!sent_parentp)
                {
                    bool remove_parent = true;
                    // No new parent, or the parent that we sent doesn't exist on the viewer.
                    LLViewerObject *parentp = (LLViewerObject *)getParent();
                    if (parentp)
                    {
                        if (parentp->getRegion() != getRegion())
                        {
                            // This is probably an object flying across a region boundary, the
                            // object probably ISN'T being reparented, but just got an object
                            // update out of order (child update before parent).
                            //LL_INFOS() << "Don't reparent object handoffs!" << LL_ENDL;
                            remove_parent = false;
                        }
                    }

                    if (remove_parent)
                    {
                        b_changed_status = true;
                        if (mDrawable.notNull())
                        {
                            // clear parent to removeChild can put the drawable on the damped list
                            setDrawableParent(NULL); // LLViewerObject::processUpdateMessage 3
                        }

                        cur_parentp->removeChild(this);

                        setChanged(MOVED | SILHOUETTE);

                        if (mDrawable.notNull())
                        {
                            // make sure this object gets a non-damped update
                            gPipeline.markMoved(mDrawable, false); // undamped
                        }
                    }
                }
            }
        }
    }

    new_rot.normQuat();

    if (sPingInterpolate && mesgsys != NULL)
    {
        LLCircuitData *cdp = gMessageSystem->mCircuitInfo.findCircuit(mesgsys->getSender());
        if (cdp)
        {
            // Note: delay is U32 and usually less then second,
            // converting it into seconds with valueInUnits will result in 0
            F32 ping_delay = 0.5f * time_dilation * ( ((F32)cdp->getPingDelay().value()) * 0.001f + gFrameDTClamped);
            LLVector3 diff = getVelocity() * ping_delay;
            new_pos_parent += diff;
        }
        else
        {
            LL_WARNS() << "findCircuit() returned NULL; skipping interpolation" << LL_ENDL;
        }
    }

    //////////////////////////
    //
    // Set the generic change flags...
    //
    //

    // If we're going to skip this message, why are we
    // doing all the parenting, etc above?
    if(mesgsys != NULL)
    {
    U32 packet_id = mesgsys->getCurrentRecvPacketID();
    if (packet_id < mLatestRecvPacketID &&
        mLatestRecvPacketID - packet_id < 65536)
    {
        //skip application of this message, it's old
        return retval;
    }
    mLatestRecvPacketID = packet_id;
    }

    // Set the change flags for scale
    if (new_scale != getScale())
    {
        setChanged(SCALED | SILHOUETTE);
        setScale(new_scale);  // Must follow setting permYouOwner()
    }

    // first, let's see if the new position is actually a change

    //static S32 counter = 0;

    F32 vel_mag_sq = getVelocity().magVecSquared();
    F32 accel_mag_sq = getAcceleration().magVecSquared();

    if (  ((b_changed_status)||(test_pos_parent != new_pos_parent))
        ||(  (!isSelected())
           &&(  (vel_mag_sq != 0.f)
              ||(accel_mag_sq != 0.f)
              ||(this_update_precision > mBestUpdatePrecision))))
    {
        mBestUpdatePrecision = this_update_precision;

        LLVector3 diff = new_pos_parent - test_pos_parent ;
        F32 mag_sqr = diff.magVecSquared() ;
        if(llfinite(mag_sqr))
        {
            setPositionParent(new_pos_parent);
        }
        else
        {
            LL_WARNS() << "Can not move the object/avatar to an infinite location!" << LL_ENDL ;

            retval |= INVALID_UPDATE ;
        }

        if (mParent && ((LLViewerObject*)mParent)->isAvatar())
        {
            // we have changed the position of an attachment, so we need to clamp it
            LLVOAvatar *avatar = (LLVOAvatar*)mParent;

            avatar->clampAttachmentPositions();
        }

        // If we're snapping the position by more than 0.5m, update LLViewerStats::mAgentPositionSnaps
        if ( asAvatar() && asAvatar()->isSelf() && (mag_sqr > 0.25f) )
        {
            record(LLStatViewer::AGENT_POSITION_SNAP, LLUnit<F64, LLUnits::Meters>(diff.length()));
        }
    }

    if ((new_rot.isNotEqualEps(getRotation(), F_ALMOST_ZERO))
        || (new_angv != old_angv))
    {
        if (new_rot != mPreviousRotation)
        {
            resetRot();
        }
        else if (new_angv != old_angv)
        {
            if (flagUsePhysics())
            {
                resetRot();
            }
            else
            {
                resetRotTime();
            }
        }

        // Remember the last rotation value
        mPreviousRotation = new_rot;

        // Set the rotation of the object followed by adjusting for the accumulated angular velocity (llSetTargetOmega)
        setRotation(new_rot * mAngularVelocityRot);
        if ((mFlags & FLAGS_SERVER_AUTOPILOT) && asAvatar() && asAvatar()->isSelf())
        {
            gAgent.resetAxes();
            gAgent.rotate(new_rot);
            gAgentCamera.resetView();
        }
        setChanged(ROTATED | SILHOUETTE);
    }

    if ( gShowObjectUpdates )
    {
        LLColor4 color;
        if (update_type == OUT_TERSE_IMPROVED)
        {
            color.setVec(0.f, 0.f, 1.f, 1.f);
        }
        else
        {
            color.setVec(1.f, 0.f, 0.f, 1.f);
        }
        gPipeline.addDebugBlip(getPositionAgent(), color);
        LL_DEBUGS("MessageBlip") << "Update type " << (S32)update_type << " blip for local " << mLocalID << " at " << getPositionAgent() << LL_ENDL;
    }

    const F32 MAG_CUTOFF = F_APPROXIMATELY_ZERO;

    llassert(vel_mag_sq >= 0.f);
    llassert(accel_mag_sq >= 0.f);
    llassert(getAngularVelocity().magVecSquared() >= 0.f);

    if ((MAG_CUTOFF >= vel_mag_sq) &&
        (MAG_CUTOFF >= accel_mag_sq) &&
        (MAG_CUTOFF >= getAngularVelocity().magVecSquared()))
    {
        mStatic = true; // This object doesn't move!
    }
    else
    {
        mStatic = false;
    }

// BUG: This code leads to problems during group rotate and any scale operation.
// Small discepencies between the simulator and viewer representations cause the
// selection center to creep, leading to objects moving around the wrong center.
//
// Removing this, however, means that if someone else drags an object you have
// selected, your selection center and dialog boxes will be wrong.  It also means
// that higher precision information on selected objects will be ignored.
//
// I believe the group rotation problem is fixed.  JNC 1.21.2002
//
    // Additionally, if any child is selected, need to update the dialogs and selection
    // center.
    bool needs_refresh = mUserSelected;
    for (child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        needs_refresh = needs_refresh || child->mUserSelected;
    }

    static LLCachedControl<bool> allow_select_avatar(gSavedSettings, "AllowSelectAvatar", false);
    if (needs_refresh)
    {
        LLSelectMgr::getInstance()->updateSelectionCenter();
        dialog_refresh_all();
    }
    else if (allow_select_avatar && asAvatar())
    {
        // Override any avatar position updates received
        // Works only if avatar was repositioned using build
        // tools and build floater is visible
        LLSelectMgr::getInstance()->overrideAvatarUpdates();
    }


    // Mark update time as approx. now, with the ping delay.
    // Ping delay is off because it's not set for velocity interpolation, causing
    // much jumping and hopping around...

//  U32 ping_delay = mesgsys->mCircuitInfo.getPingDelay();
    mLastInterpUpdateSecs = LLFrameTimer::getElapsedSeconds();
    mLastMessageUpdateSecs = mLastInterpUpdateSecs;
    if (mDrawable.notNull())
    {
        // Don't clear invisibility flag on update if still orphaned!
        if (mDrawable->isState(LLDrawable::FORCE_INVISIBLE) && !mOrphaned)
        {
//          LL_DEBUGS() << "Clearing force invisible: " << mID << ":" << getPCodeString() << ":" << getPositionAgent() << LL_ENDL;
            mDrawable->clearState(LLDrawable::FORCE_INVISIBLE);
            gPipeline.markRebuild( mDrawable, LLDrawable::REBUILD_ALL);
        }
    }

    // Update special hover cursor status
    bool special_hover_cursor = specialHoverCursor();
    if (old_special_hover_cursor != special_hover_cursor
        && mDrawable.notNull())
    {
        mDrawable->updateSpecialHoverCursor(special_hover_cursor);
    }

    return retval;
}

bool LLViewerObject::isActive() const
{
    return true;
}

//load flags from cache or from message
void LLViewerObject::loadFlags(U32 flags)
{
    if(flags == (U32)(-1))
    {
        return; //invalid
    }

    // keep local flags and overwrite remote-controlled flags
    mFlags = (mFlags & FLAGS_LOCAL) | flags;

    // ...new objects that should come in selected need to be added to the selected list
    mCreateSelected = ((flags & FLAGS_CREATE_SELECTED) != 0);
    return;
}

void LLViewerObject::idleUpdate(LLAgent &agent, const F64 &frame_time)
{
    if (!mDead)
    {
        if (!mStatic && sVelocityInterpolate && !isSelected())
        {
            // calculate dt from last update
            F32 time_dilation = mRegionp ? mRegionp->getTimeDilation() : 1.0f;
            F32 dt_raw = (F32)((F64Seconds)frame_time - mLastInterpUpdateSecs).value();
            F32 dt = time_dilation * dt_raw;

            applyAngularVelocity(dt);

            if (isAttachment())
            {
                mLastInterpUpdateSecs = (F64Seconds)frame_time;
                return;
            }
            else
            {   // Move object based on it's velocity and rotation
                interpolateLinearMotion(frame_time, dt);
            }
        }

        updateDrawable(false);
    }
}


// Move an object due to idle-time viewer side updates by interpolating motion
void LLViewerObject::interpolateLinearMotion(const F64SecondsImplicit& frame_time, const F32SecondsImplicit& dt_seconds)
{
    // linear motion
    // PHYSICS_TIMESTEP is used below to correct for the fact that the velocity in object
    // updates represents the average velocity of the last timestep, rather than the final velocity.
    // the time dilation above should guarantee that dt is never less than PHYSICS_TIMESTEP, theoretically
    //
    // *TODO: should also wrap linear accel/velocity in check
    // to see if object is selected, instead of explicitly
    // zeroing it out

    F32 dt = dt_seconds;
    F64Seconds time_since_last_update = frame_time - mLastMessageUpdateSecs;
    if (time_since_last_update <= (F64Seconds)0.0 || dt <= 0.f)
    {
        return;
    }

    LLVector3 accel = getAcceleration();
    LLVector3 vel   = getVelocity();

    if (sMaxUpdateInterpolationTime <= (F64Seconds)0.0)
    {   // Old code path ... unbounded, simple interpolation
        if (!(accel.isExactlyZero() && vel.isExactlyZero()))
        {
            LLVector3 pos   = (vel + (0.5f * (dt-PHYSICS_TIMESTEP)) * accel) * dt;

            // region local
            setPositionRegion(pos + getPositionRegion());
            setVelocity(vel + accel*dt);

            // for objects that are spinning but not translating, make sure to flag them as having moved
            setChanged(MOVED | SILHOUETTE);
        }
    }
    else if (!accel.isExactlyZero() || !vel.isExactlyZero())        // object is moving
    {   // Object is moving, and hasn't been too long since we got an update from the server

        // Calculate predicted position and velocity
        LLVector3 new_pos = (vel + (0.5f * (dt-PHYSICS_TIMESTEP)) * accel) * dt;
        LLVector3 new_v = accel * dt;

        if (time_since_last_update > sPhaseOutUpdateInterpolationTime &&
            sPhaseOutUpdateInterpolationTime > (F64Seconds)0.0)
        {   // Haven't seen a viewer update in a while, check to see if the circuit is still active
            if (mRegionp)
            {   // The simulator will NOT send updates if the object continues normally on the path
                // predicted by the velocity and the acceleration (often gravity) sent to the viewer
                // So check to see if the circuit is blocked, which means the sim is likely in a long lag
                LLCircuitData *cdp = gMessageSystem->mCircuitInfo.findCircuit( mRegionp->getHost() );
                if (cdp)
                {
                    // Find out how many seconds since last packet arrived on the circuit
                    F64Seconds time_since_last_packet = LLMessageSystem::getMessageTimeSeconds() - cdp->getLastPacketInTime();

                    if (!cdp->isAlive() ||      // Circuit is dead or blocked
                         cdp->isBlocked() ||    // or doesn't seem to be getting any packets
                         (time_since_last_packet > sPhaseOutUpdateInterpolationTime))
                    {
                        // Start to reduce motion interpolation since we haven't seen a server update in a while
                        F64Seconds time_since_last_interpolation = frame_time - mLastInterpUpdateSecs;
                        F64 phase_out = 1.0;
                        if (time_since_last_update > sMaxUpdateInterpolationTime)
                        {   // Past the time limit, so stop the object
                            phase_out = 0.0;
                            //LL_INFOS() << "Motion phase out to zero" << LL_ENDL;

                            // Kill angular motion as well.  Note - not adding this due to paranoia
                            // about stopping rotation for llTargetOmega objects and not having it restart
                            // setAngularVelocity(LLVector3::zero);
                        }
                        else if (mLastInterpUpdateSecs - mLastMessageUpdateSecs > sPhaseOutUpdateInterpolationTime)
                        {   // Last update was already phased out a bit
                            phase_out = (sMaxUpdateInterpolationTime - time_since_last_update) /
                                        (sMaxUpdateInterpolationTime - time_since_last_interpolation);
                            //LL_INFOS() << "Continuing motion phase out of " << (F32) phase_out << LL_ENDL;
                        }
                        else
                        {   // Phase out from full value
                            phase_out = (sMaxUpdateInterpolationTime - time_since_last_update) /
                                        (sMaxUpdateInterpolationTime - sPhaseOutUpdateInterpolationTime);
                            //LL_INFOS() << "Starting motion phase out of " << (F32) phase_out << LL_ENDL;
                        }
                        phase_out = llclamp(phase_out, 0.0, 1.0);

                        new_pos = new_pos * ((F32) phase_out);
                        new_v = new_v * ((F32) phase_out);
                    }
                }
            }
        }

        new_pos = new_pos + getPositionRegion();
        new_v = new_v + vel;


        // Clamp interpolated position to minimum underground and maximum region height
        LLVector3d new_pos_global = mRegionp->getPosGlobalFromRegion(new_pos);
        F32 min_height;
        if (isAvatar())
        {   // Make a better guess about AVs not going underground
            min_height = LLWorld::getInstance()->resolveLandHeightGlobal(new_pos_global);
            min_height += (0.5f * getScale().mV[VZ]);
        }
        else
        {   // This will put the object underground, but we can't tell if it will stop
            // at ground level or not
            min_height = LLWorld::getInstance()->getMinAllowedZ(this, new_pos_global);
            // Cap maximum height
            new_pos.mV[VZ] = llmin(LLWorld::getInstance()->getRegionMaxHeight(), new_pos.mV[VZ]);
        }

        new_pos.mV[VZ] = llmax(min_height, new_pos.mV[VZ]);

        // Check to see if it's going off the region
        LLVector3 temp(new_pos.mV[VX], new_pos.mV[VY], 0.f);
        if (temp.clamp(0.f, mRegionp->getWidth()))
        {   // Going off this region, so see if we might end up on another region
            LLVector3d old_pos_global = mRegionp->getPosGlobalFromRegion(getPositionRegion());
            new_pos_global = mRegionp->getPosGlobalFromRegion(new_pos);     // Re-fetch in case it got clipped above

            // Clip the positions to known regions
            LLVector3d clip_pos_global = LLWorld::getInstance()->clipToVisibleRegions(old_pos_global, new_pos_global);
            if (clip_pos_global != new_pos_global)
            {
                // Was clipped, so this means we hit a edge where there is no region to enter
                LLVector3 clip_pos = mRegionp->getPosRegionFromGlobal(clip_pos_global);
                LL_DEBUGS("Interpolate") << "Hit empty region edge, clipped predicted position to "
                                         << clip_pos
                                         << " from " << new_pos << LL_ENDL;
                new_pos = clip_pos;

                // Stop motion and get server update for bouncing on the edge
                new_v.clear();
                setAcceleration(LLVector3::zero);
            }
            else
            {
                // Check for how long we are crossing.
                // Note: theoretically we can find time from velocity, acceleration and
                // distance from border to new position, but it is not going to work
                // if 'phase_out' activates
                if (mRegionCrossExpire == 0)
                {
                    // Workaround: we can't accurately figure out time when we cross border
                    // so just write down time 'after the fact', it is far from optimal in
                    // case of lags, but for lags sMaxUpdateInterpolationTime will kick in first
                    LL_DEBUGS("Interpolate") << "Predicted region crossing, new position " << new_pos << LL_ENDL;
                    mRegionCrossExpire = frame_time + sMaxRegionCrossingInterpolationTime;
                }
                else if (frame_time > mRegionCrossExpire)
                {
                    // Predicting crossing over 1s, stop motion
                    // Stop motion
                    LL_DEBUGS("Interpolate") << "Predicting region crossing for too long, stopping at " << new_pos << LL_ENDL;
                    new_v.clear();
                    setAcceleration(LLVector3::zero);
                    mRegionCrossExpire = 0;
                }
            }
        }
        else
        {
            mRegionCrossExpire = 0;
        }

        // Set new position and velocity
        setPositionRegion(new_pos);
        setVelocity(new_v);

        // for objects that are spinning but not translating, make sure to flag them as having moved
        setChanged(MOVED | SILHOUETTE);
    }

    // Update the last time we did anything
    mLastInterpUpdateSecs = frame_time;
}



// delete an item in the inventory, but don't tell the server. This is
// used internally by remove, update, and savescript.
// This will only delete the first item with an item_id in the list
void LLViewerObject::deleteInventoryItem(const LLUUID& item_id)
{
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it )
        {
            if((*it)->getUUID() == item_id)
            {
                // This is safe only because we return immediatly.
                mInventory->erase(it); // will deref and delete it
                return;
            }
        }
        doInventoryCallback();
    }
}

void LLViewerObject::doUpdateInventory(
    LLPointer<LLViewerInventoryItem>& item,
    U8 key,
    bool is_new)
{
    LLViewerInventoryItem* old_item = NULL;
    if(TASK_INVENTORY_ITEM_KEY == key)
    {
        old_item = (LLViewerInventoryItem*)getInventoryObject(item->getUUID());
    }
    else if(TASK_INVENTORY_ASSET_KEY == key)
    {
        old_item = getInventoryItemByAsset(item->getAssetUUID());
    }
    LLUUID item_id;
    LLUUID new_owner;
    LLUUID new_group;
    bool group_owned = false;
    if(old_item)
    {
        item_id = old_item->getUUID();
        new_owner = old_item->getPermissions().getOwner();
        new_group = old_item->getPermissions().getGroup();
        group_owned = old_item->getPermissions().isGroupOwned();
        old_item = NULL;
    }
    else
    {
        item_id = item->getUUID();
    }
    if(!is_new && mInventory)
    {
        // Attempt to update the local inventory. If we can get the
        // object perm, we have perfect visibility, so we want the
        // serial number to match. Otherwise, take our best guess and
        // make sure that the serial number does not match.
        deleteInventoryItem(item_id);
        LLPermissions perm(item->getPermissions());
        LLPermissions* obj_perm = LLSelectMgr::getInstance()->findObjectPermissions(this);
        bool is_atomic = (S32)LLAssetType::AT_OBJECT != item->getType();
        if(obj_perm)
        {
            perm.setOwnerAndGroup(LLUUID::null, obj_perm->getOwner(), obj_perm->getGroup(), is_atomic);
        }
        else
        {
            if(group_owned)
            {
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            else if(!new_owner.isNull())
            {
                // The object used to be in inventory, so we can
                // assume the owner and group will match what they are
                // there.
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            // *FIX: can make an even better guess by using the mPermGroup flags
            else if(permYouOwner())
            {
                // best guess.
                perm.setOwnerAndGroup(LLUUID::null, gAgent.getID(), item->getPermissions().getGroup(), is_atomic);
                --mExpectedInventorySerialNum;
            }
            else
            {
                // dummy it up.
                perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, LLUUID::null, is_atomic);
                --mExpectedInventorySerialNum;
            }
        }
        LLViewerInventoryItem* oldItem = item;
        LLViewerInventoryItem* new_item = new LLViewerInventoryItem(oldItem);
        new_item->setPermissions(perm);
        mInventory->push_front(new_item);
        doInventoryCallback();
        ++mExpectedInventorySerialNum;
    }
    else if (is_new)
    {
        ++mExpectedInventorySerialNum;
    }
}

// save a script, which involves removing the old one, and rezzing
// in the new one. This method should be called with the asset id
// of the new and old script AFTER the bytecode has been saved.
void LLViewerObject::saveScript(
    const LLViewerInventoryItem* item,
    bool active,
    bool is_new)
{
    /*
     * XXXPAM Investigate not making this copy.  Seems unecessary, but I'm unsure about the
     * interaction with doUpdateInventory() called below.
     */
    LL_DEBUGS() << "LLViewerObject::saveScript() " << item->getUUID() << " " << item->getAssetUUID() << LL_ENDL;

    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RezScript);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
    msg->nextBlockFast(_PREHASH_UpdateBlock);
    msg->addU32Fast(_PREHASH_ObjectLocalID, (mLocalID));
    U8 enabled = active;
    msg->addBOOLFast(_PREHASH_Enabled, enabled);
    msg->nextBlockFast(_PREHASH_InventoryBlock);
    task_item->packMessage(msg);
    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, is_new);
}

void LLViewerObject::moveInventory(const LLUUID& folder_id,
                                   const LLUUID& item_id)
{
    LL_DEBUGS() << "LLViewerObject::moveInventory " << item_id << LL_ENDL;
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_FolderID, folder_id);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());

    LLInventoryObject* inv_obj = getInventoryObject(item_id);
    if(inv_obj)
    {
        LLViewerInventoryItem* item = (LLViewerInventoryItem*)inv_obj;
        if(!item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            deleteInventoryItem(item_id);
            ++mExpectedInventorySerialNum;
        }
    }
}

void LLViewerObject::dirtyInventory()
{
    // If there aren't any LLVOInventoryListeners, we won't be
    // able to update our mInventory when it comes back from the
    // simulator, so we should not clear the inventory either.
    if(mInventory && !mInventoryCallbacks.empty())
    {
        mInventory->clear(); // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }
    mInventoryDirty = true;
}

void LLViewerObject::registerInventoryListener(LLVOInventoryListener* listener, void* user_data)
{
    LLInventoryCallbackInfo* info = new LLInventoryCallbackInfo;
    info->mListener = listener;
    info->mInventoryData = user_data;
    mInventoryCallbacks.push_front(info);
}

void LLViewerObject::removeInventoryListener(LLVOInventoryListener* listener)
{
    if (listener == NULL)
        return;
    for (callback_list_t::iterator iter = mInventoryCallbacks.begin();
         iter != mInventoryCallbacks.end(); )
    {
        callback_list_t::iterator curiter = iter++;
        LLInventoryCallbackInfo* info = *curiter;
        if (info->mListener == listener)
        {
            delete info;
            mInventoryCallbacks.erase(curiter);
            break;
        }
    }
}

bool LLViewerObject::isInventoryPending()
{
    return mInvRequestState != INVENTORY_REQUEST_STOPPED;
}

void LLViewerObject::clearInventoryListeners()
{
    for_each(mInventoryCallbacks.begin(), mInventoryCallbacks.end(), DeletePointer());
    mInventoryCallbacks.clear();
}

bool LLViewerObject::hasInventoryListeners()
{
    return !mInventoryCallbacks.empty();
}

void LLViewerObject::requestInventory()
{
    if(mInventoryDirty && mInventory && !mInventoryCallbacks.empty())
    {
        mInventory->clear(); // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }

    if(mInventory)
    {
        // inventory is either up to date or doesn't has a listener
        // if it is dirty, leave it this way in case we gain a listener
        doInventoryCallback();
    }
    else
    {
        // since we are going to request it now
        mInventoryDirty = false;

        // Note: throws away duplicate requests
        fetchInventoryFromServer();
    }
}

void LLViewerObject::fetchInventoryFromServer()
{
    if (!isInventoryPending())
    {
        delete mInventory;
        mInventory = NULL;

        // This will get reset by doInventoryCallback or processTaskInv
        mInvRequestState = INVENTORY_REQUEST_PENDING;

        if (mRegionp && !mRegionp->getCapability("RequestTaskInventory").empty())
        {
            LLCoros::instance().launch("LLViewerObject::fetchInventoryFromCapCoro()",
                                       boost::bind(&LLViewerObject::fetchInventoryFromCapCoro, mID));
        }
        else
        {
            LL_WARNS() << "Using old task inventory path!" << LL_ENDL;
        // Results in processTaskInv
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_RequestTaskInventory);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_InventoryData);
        msg->addU32Fast(_PREHASH_LocalID, mLocalID);
        msg->sendReliable(mRegionp->getHost());
        }
    }
}

void LLViewerObject::fetchInventoryDelayed(const F64 &time_seconds)
{
    // unless already waiting, drop previous request and schedule an update
    if (mInvRequestState != INVENTORY_REQUEST_WAIT)
    {
        if (mInvRequestXFerId != 0)
        {
            // abort download.
            gXferManager->abortRequestById(mInvRequestXFerId, -1);
            mInvRequestXFerId = 0;
        }
        mInvRequestState = INVENTORY_REQUEST_WAIT; // affects isInventoryPending()
        LLCoros::instance().launch("LLViewerObject::fetchInventoryDelayedCoro()",
            boost::bind(&LLViewerObject::fetchInventoryDelayedCoro, mID, time_seconds));
    }
}

//static
void LLViewerObject::fetchInventoryDelayedCoro(const LLUUID task_inv, const F64 time_seconds)
{
    llcoro::suspendUntilTimeout((float)time_seconds);
    LLViewerObject *obj = gObjectList.findObject(task_inv);
    if (obj)
    {
        // Might be good idea to prolong delay here in case expected serial changed.
        // As it is, it will get a response with obsolete serial and will delay again.

        // drop waiting state to unlock isInventoryPending()
        obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
        obj->fetchInventoryFromServer();
    }
}

//static
void LLViewerObject::fetchInventoryFromCapCoro(const LLUUID task_inv)
{
    LLViewerObject *obj = gObjectList.findObject(task_inv);
    if (obj)
    {
        LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
                                   httpAdapter(new LLCoreHttpUtil::HttpCoroutineAdapter("TaskInventoryRequest", httpPolicy));
        LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);
        std::string url = obj->mRegionp->getCapability("RequestTaskInventory") + "?task_id=" + obj->mID.asString();
        // If we already have a copy of the inventory then add it so the server won't re-send something we already have.
        // We expect this case to crop up in the case of failed inventory mutations, but it might happen otherwise as well.
        if (obj->mInventorySerialNum && obj->mInventory)
            url += "&inventory_serial=" + std::to_string(obj->mInventorySerialNum);

        obj->mInvRequestState = INVENTORY_XFER;
        LLSD result = httpAdapter->getAndSuspend(httpRequest, url);

        LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

        // Object may have gone away while we were suspended, double-check that it still exists
        obj = gObjectList.findObject(task_inv);
        if (!obj)
        {
            LL_WARNS() << "Object " << task_inv << " went away while fetching inventory, dropping result" << LL_ENDL;
            return;
        }

        bool potentially_stale = false;
        if (status)
        {
            // Dealing with inventory serials is kind of funky. They're monotonically increasing and 16 bits,
            // so we expect them to overflow, but we can use inv serial < expected serial as a signal that we may
            // have mutated the task inventory since we kicked off the request, and those mutations may have not
            // been taken into account yet. Of course, those mutations may have actually failed which would result
            // in the inv serial never increasing.
            //
            // When we detect this case, set the expected inv serial to the inventory serial we actually received
            // and kick off a re-request after a slight delay.
            S16 serial = (S16)result["inventory_serial"].asInteger();
            potentially_stale = serial < obj->mExpectedInventorySerialNum;
            LL_INFOS() << "Inventory loaded for " << task_inv << LL_ENDL;
            obj->mInventorySerialNum = serial;
            obj->mExpectedInventorySerialNum = serial;
            obj->loadTaskInvLLSD(result);
        }
        else if (status.getType() == 304)
        {
            LL_INFOS() << "Inventory wasn't changed on server!" << LL_ENDL;
            obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
            // Even though it wasn't necessary to send a response, we still may have mutated
            // the inventory since we kicked off the request, check for that case.
            potentially_stale = obj->mInventorySerialNum < obj->mExpectedInventorySerialNum;
            // Set this to what we already have so that we don't re-request a second time.
            obj->mExpectedInventorySerialNum = obj->mInventorySerialNum;
        }
        else
        {
            // Not sure that there's anything sensible we can do to recover here, retrying in a loop would be bad.
            LL_WARNS() << "Error status while requesting task inventory: " << status.toString() << LL_ENDL;
            obj->mInvRequestState = INVENTORY_REQUEST_STOPPED;
        }

        if (potentially_stale)
        {
            // Stale? I guess we can use what we got for now, but we'll have to re-request
            LL_WARNS() << "Stale inv_serial? Re-requesting." << LL_ENDL;
            obj->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_OUTDATED);
        }
    }
}

LLControlAvatar *LLViewerObject::getControlAvatar()
{
    return getRootEdit()->mControlAvatar.get();
}

LLControlAvatar *LLViewerObject::getControlAvatar() const
{
    return getRootEdit()->mControlAvatar.get();
}

// Manage the control avatar state of a given object.
// Any object can be flagged as animated, but for performance reasons
// we don't want to incur the overhead of managing a control avatar
// unless this would have some user-visible consequence. That is,
// there should be at least one rigged mesh in the linkset. Operations
// that change the state of a linkset, such as linking or unlinking
// prims, can also mean that a control avatar needs to be added or
// removed. At the end, if there is a control avatar, we make sure
// that its animation state is current.
void LLViewerObject::updateControlAvatar()
{
    LLViewerObject *root = getRootEdit();
    bool is_animated_object = root->isAnimatedObject();
    bool has_control_avatar = getControlAvatar();
    if (!is_animated_object && !has_control_avatar)
    {
        return;
    }

    // caller isn't supposed to operate on a dead object,
    // avatar was already cleaned up
    llassert(!isDead());

    bool should_have_control_avatar = false;
    if (is_animated_object)
    {
        bool any_rigged_mesh = root->isRiggedMesh();
        LLViewerObject::const_child_list_t& child_list = root->getChildren();
        for (LLViewerObject::const_child_list_t::const_iterator iter = child_list.begin();
             iter != child_list.end(); ++iter)
        {
            const LLViewerObject* child = *iter;
            any_rigged_mesh = any_rigged_mesh || child->isRiggedMesh();
        }
        should_have_control_avatar = is_animated_object && any_rigged_mesh;
    }

    if (should_have_control_avatar && !has_control_avatar)
    {
        std::string vobj_name = llformat("Vol%p", root);
        LL_DEBUGS("AnimatedObjects") << vobj_name << " calling linkControlAvatar()" << LL_ENDL;
        root->linkControlAvatar();
    }
    if (!should_have_control_avatar && has_control_avatar)
    {
        std::string vobj_name = llformat("Vol%p", root);
        LL_DEBUGS("AnimatedObjects") << vobj_name << " calling unlinkControlAvatar()" << LL_ENDL;
        root->unlinkControlAvatar();
    }
    if (getControlAvatar())
    {
        getControlAvatar()->updateAnimations();
        if (isSelected())
        {
            LLSelectMgr::getInstance()->pauseAssociatedAvatars();
        }
    }
}

void LLViewerObject::linkControlAvatar()
{
    if (!getControlAvatar() && isRootEdit())
    {
        LLVOVolume *volp = dynamic_cast<LLVOVolume*>(this);
        if (!volp)
        {
            LL_WARNS() << "called with null or non-volume object" << LL_ENDL;
            return;
        }
        mControlAvatar = LLControlAvatar::createControlAvatar(volp);
        LL_DEBUGS("AnimatedObjects") << volp->getID()
                                     << " created control av for "
                                     << (S32) (1+volp->numChildren()) << " prims" << LL_ENDL;
    }
    LLControlAvatar *cav = getControlAvatar();
    if (cav)
    {
        cav->updateAttachmentOverrides();
        if (!cav->mPlaying)
        {
            cav->mPlaying = true;
            //if (!cav->mRootVolp->isAnySelected())
            {
                cav->updateVolumeGeom();
                cav->mRootVolp->recursiveMarkForUpdate();
            }
        }
    }
    else
    {
        LL_WARNS() << "no control avatar found!" << LL_ENDL;
    }
}

void LLViewerObject::unlinkControlAvatar()
{
    if (getControlAvatar())
    {
        getControlAvatar()->updateAttachmentOverrides();
    }
    if (isRootEdit())
    {
        // This will remove the entire linkset from the control avatar
        if (mControlAvatar)
        {
            mControlAvatar->markForDeath();
            mControlAvatar = NULL;
        }
    }
    // For non-root prims, removing from the linkset will
    // automatically remove the control avatar connection.
}

// virtual
bool LLViewerObject::isAnimatedObject() const
{
    return false;
}

struct LLFilenameAndTask
{
    LLUUID mTaskID;
    std::string mFilename;

    // for sequencing in case of multiple updates
    S16 mSerial;
#ifdef _DEBUG
    static S32 sCount;
    LLFilenameAndTask()
    {
        ++sCount;
        LL_DEBUGS() << "Constructing LLFilenameAndTask: " << sCount << LL_ENDL;
    }
    ~LLFilenameAndTask()
    {
        --sCount;
        LL_DEBUGS() << "Destroying LLFilenameAndTask: " << sCount << LL_ENDL;
    }
private:
    LLFilenameAndTask(const LLFilenameAndTask& rhs);
    const LLFilenameAndTask& operator=(const LLFilenameAndTask& rhs) const;
#endif
};

#ifdef _DEBUG
S32 LLFilenameAndTask::sCount = 0;
#endif

// static
void LLViewerObject::processTaskInv(LLMessageSystem* msg, void** user_data)
{
    LLUUID task_id;
    msg->getUUIDFast(_PREHASH_InventoryData, _PREHASH_TaskID, task_id);
    LLViewerObject* object = gObjectList.findObject(task_id);
    if (!object)
    {
        LL_WARNS() << "LLViewerObject::processTaskInv object "
            << task_id << " does not exist." << LL_ENDL;
        return;
    }

    // we can receive multiple task updates simultaneously, make sure we will not rewrite newer with older update
    S16 serial = 0;
    msg->getS16Fast(_PREHASH_InventoryData, _PREHASH_Serial, serial);

    if (object->mRegionp && !object->mRegionp->getCapability("RequestTaskInventory").empty())
    {
        // It seems that simulator may ask us to re-download the task inventory if an update to the inventory
        // happened out-of-band while we had the object selected (like if a script is saved.)
        //
        // If we're meant to use the HTTP capability, ignore the contents of the UDP message and fetch the
        // inventory via the CAP so that we don't flow down the UDP inventory request path unconditionally here.
        // We shouldn't need to wait, as any updates should already be ready to fetch by this point.
        LL_INFOS() << "Handling unsolicited ReplyTaskInventory for " << task_id << LL_ENDL;
        object->mExpectedInventorySerialNum = serial;
        object->fetchInventoryFromServer();
        return;
    }

    if (serial == object->mInventorySerialNum
        && serial < object->mExpectedInventorySerialNum)
    {
        // Loop Protection.
        // We received same serial twice.
        // Viewer did some changes to inventory that couldn't be saved server side
        // or something went wrong to cause serial to be out of sync.
        // Drop xfer and restart after some time, assign server's value as expected
        LL_WARNS() << "Task inventory serial might be out of sync, server serial: " << serial << " client expected serial: " << object->mExpectedInventorySerialNum << LL_ENDL;
        object->mExpectedInventorySerialNum = serial;
        object->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_DESYNC);
    }
    else if (serial < object->mExpectedInventorySerialNum)
    {
        // Out of date message, record to current serial for loop protection, but do not load it
        // Drop xfer and restart after some time
        if (serial < object->mInventorySerialNum)
        {
            LL_WARNS() << "Task serial decreased. Potentially out of order packet or desync." << LL_ENDL;
        }
        object->mInventorySerialNum = serial;
        object->fetchInventoryDelayed(INVENTORY_UPDATE_WAIT_TIME_OUTDATED);
    }
    else if (serial >= object->mExpectedInventorySerialNum)
    {
        LLFilenameAndTask* ft = new LLFilenameAndTask;
        ft->mTaskID = task_id;
        ft->mSerial = serial;

        // We received version we expected or newer. Load it.
        object->mInventorySerialNum = ft->mSerial;
        object->mExpectedInventorySerialNum = ft->mSerial;

        std::string unclean_filename;
        msg->getStringFast(_PREHASH_InventoryData, _PREHASH_Filename, unclean_filename);
        ft->mFilename = LLDir::getScrubbedFileName(unclean_filename);

        if (ft->mFilename.empty())
        {
            LL_DEBUGS() << "Task has no inventory" << LL_ENDL;
            // mock up some inventory to make a drop target.
            if (object->mInventory)
            {
                object->mInventory->clear(); // will deref and delete it
            }
            else
            {
                object->mInventory = new LLInventoryObject::object_list_t();
            }
            LLPointer<LLInventoryObject> obj;
            obj = new LLInventoryObject(object->mID, LLUUID::null,
                LLAssetType::AT_CATEGORY,
                "Contents");
            object->mInventory->push_front(obj);
            object->doInventoryCallback();
            delete ft;
            return;
        }
        U64 new_id = gXferManager->requestFile(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ft->mFilename),
            ft->mFilename, LL_PATH_CACHE,
            object->mRegionp->getHost(),
            true,
            &LLViewerObject::processTaskInvFile,
            (void**)ft, // This takes ownership of ft
            LLXferManager::HIGH_PRIORITY);
        if (object->mInvRequestState == INVENTORY_XFER)
        {
            if (new_id > 0 && new_id != object->mInvRequestXFerId)
            {
                // we started new download.
                gXferManager->abortRequestById(object->mInvRequestXFerId, -1);
                object->mInvRequestXFerId = new_id;
            }
        }
        else
        {
            object->mInvRequestState = INVENTORY_XFER;
            object->mInvRequestXFerId = new_id;
        }
    }
}

void LLViewerObject::processTaskInvFile(void** user_data, S32 error_code, LLExtStat ext_status)
{
    LLFilenameAndTask* ft = (LLFilenameAndTask*)user_data;
    LLViewerObject* object = NULL;

    if (ft
        && (0 == error_code)
        && (object = gObjectList.findObject(ft->mTaskID))
        && ft->mSerial >= object->mInventorySerialNum)
    {
        object->mInventorySerialNum = ft->mSerial;
        LL_DEBUGS() << "Receiving inventory task file for serial " << object->mInventorySerialNum << " taskid: " << ft->mTaskID << LL_ENDL;
        if (ft->mSerial < object->mExpectedInventorySerialNum)
        {
            // User managed to change something while inventory was loading
            LL_DEBUGS() << "Processing file that is potentially out of date for task: " << ft->mTaskID << LL_ENDL;
        }

        if (object->loadTaskInvFile(ft->mFilename))
        {

            LLInventoryObject::object_list_t::iterator it = object->mInventory->begin();
            LLInventoryObject::object_list_t::iterator end = object->mInventory->end();
            std::list<LLUUID>& pending_lst = object->mPendingInventoryItemsIDs;

            for (; it != end && pending_lst.size(); ++it)
            {
                LLViewerInventoryItem* item = dynamic_cast<LLViewerInventoryItem*>(it->get());
                if(item && item->getType() != LLAssetType::AT_CATEGORY)
                {
                    std::list<LLUUID>::iterator id_it = std::find(pending_lst.begin(), pending_lst.begin(), item->getAssetUUID());
                    if (id_it != pending_lst.end())
                    {
                        pending_lst.erase(id_it);
                    }
                }
            }
        }
        else
        {
            // MAINT-2597 - crash when trying to edit a no-mod object
            // Somehow get an contents inventory response, but with an invalid stream (possibly 0 size?)
            // Stated repro was specific to no-mod objects so failing without user interaction should be safe.
            LL_WARNS() << "Trying to load invalid task inventory file. Ignoring file contents." << LL_ENDL;
        }
    }
    else
    {
        // This Occurs When two requests were made, and the first one
        // has already handled it.
        LL_DEBUGS() << "Problem loading task inventory. Return code: "
                 << error_code << LL_ENDL;
    }
    delete ft;
}

bool LLViewerObject::loadTaskInvFile(const std::string& filename)
{
    std::string filename_and_local_path = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, filename);
    llifstream ifs(filename_and_local_path.c_str());
    if(ifs.good())
    {
        U32 fail_count = 0;
        char buffer[MAX_STRING];    /* Flawfinder: ignore */
        // *NOTE: This buffer size is hard coded into scanf() below.
        char keyword[MAX_STRING];   /* Flawfinder: ignore */
        if(mInventory)
        {
            mInventory->clear(); // will deref and delete it
        }
        else
        {
            mInventory = new LLInventoryObject::object_list_t;
        }
        while(ifs.good())
        {
            ifs.getline(buffer, MAX_STRING);
            if (sscanf(buffer, " %254s", keyword) == EOF) /* Flawfinder: ignore */
            {
                // Blank file?
                LL_WARNS() << "Issue reading from file '"
                        << filename << "'" << LL_ENDL;
                break;
            }
            else if(0 == strcmp("inv_item", keyword))
            {
                LLPointer<LLInventoryObject> inv = new LLViewerInventoryItem;
                inv->importLegacyStream(ifs);
                mInventory->push_front(inv);
            }
            else if(0 == strcmp("inv_object", keyword))
            {
                LLPointer<LLInventoryObject> inv = new LLInventoryObject;
                inv->importLegacyStream(ifs);
                inv->rename("Contents");
                mInventory->push_front(inv);
            }
            else if (fail_count >= MAX_INV_FILE_READ_FAILS)
            {
                LL_WARNS() << "Encountered too many unknowns while reading from file: '"
                        << filename << "'" << LL_ENDL;
                break;
            }
            else
            {
                // Is there really a point to continue processing? We already failing to display full inventory
                fail_count++;
                LL_WARNS_ONCE() << "Unknown token while reading from inventory file. Token: '"
                        << keyword << "'" << LL_ENDL;
            }
        }
        ifs.close();
        LLFile::remove(filename_and_local_path);
    }
    else
    {
        LL_WARNS() << "unable to load task inventory: " << filename_and_local_path
                << LL_ENDL;
        return false;
    }
    doInventoryCallback();

    return true;
}

void LLViewerObject::loadTaskInvLLSD(const LLSD& inv_result)
{
    if (inv_result.has("contents"))
    {
        if(mInventory)
        {
            mInventory->clear(); // will deref and delete it
        }
        else
        {
            mInventory = new LLInventoryObject::object_list_t;
        }

        // Synthesize the "Contents" category, the viewer expects it, but it isn't sent.
        LLPointer<LLInventoryObject> inv = new LLInventoryObject(mID, LLUUID::null, LLAssetType::AT_CATEGORY, "Contents");
        mInventory->push_front(inv);

        const LLSD& inventory = inv_result["contents"];
        for (const auto& inv_entry : llsd::inArray(inventory))
        {
            if (inv_entry.has("item_id"))
            {
                LLPointer<LLViewerInventoryItem> inv = new LLViewerInventoryItem;
                inv->unpackMessage(inv_entry);
                mInventory->push_front(inv);
            }
            else
            {
                LL_WARNS_ONCE() << "Unknown inventory entry while reading from inventory file. Entry: '"
                                << inv_entry << "'" << LL_ENDL;
            }
        }
    }
    else
    {
        LL_WARNS() << "unable to load task inventory: " << inv_result << LL_ENDL;
        return;
    }
    doInventoryCallback();
}

void LLViewerObject::doInventoryCallback()
{
    for (callback_list_t::iterator iter = mInventoryCallbacks.begin();
         iter != mInventoryCallbacks.end(); )
    {
        callback_list_t::iterator curiter = iter++;
        LLInventoryCallbackInfo* info = *curiter;
        if (info->mListener != NULL)
        {
            info->mListener->inventoryChanged(this,
                                 mInventory,
                                 mInventorySerialNum,
                                 info->mInventoryData);
        }
        else
        {
            LL_INFOS() << "LLViewerObject::doInventoryCallback() deleting bad listener entry." << LL_ENDL;
            delete info;
            mInventoryCallbacks.erase(curiter);
        }
    }

    // release inventory loading state
    mInvRequestXFerId = 0;
    mInvRequestState = INVENTORY_REQUEST_STOPPED;
}

void LLViewerObject::removeInventory(const LLUUID& item_id)
{
    // close associated floater properties
    LLSD params;
    params["id"] = item_id;
    params["object"] = mID;
    LLFloaterReg::hideInstance("item_properties", params);

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RemoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());
    deleteInventoryItem(item_id);
    ++mExpectedInventorySerialNum;
}

bool LLViewerObject::isAssetInInventory(LLViewerInventoryItem* item, LLAssetType::EType type)
{
    bool result = false;

    if (item)
    {
        // For now mPendingInventoryItemsIDs only stores textures and materials
        // but if it gets to store more types, it will need to verify type as well
        // since null can be a shared default id and it is fine to need a null
        // script and a null material simultaneously.
        std::list<LLUUID>::iterator begin = mPendingInventoryItemsIDs.begin();
        std::list<LLUUID>::iterator end = mPendingInventoryItemsIDs.end();

        bool is_fetching = std::find(begin, end, item->getAssetUUID()) != end;

        // null is the default asset for materials and default for scripts
        // so need to check type as well
        bool is_fetched = getInventoryItemByAsset(item->getAssetUUID(), type) != NULL;

        result = is_fetched || is_fetching;
    }

    return result;
}

void LLViewerObject::updateMaterialInventory(LLViewerInventoryItem* item, U8 key, bool is_new)
{
    if (!item)
    {
        return;
    }
    if (LLAssetType::AT_TEXTURE != item->getType()
        && LLAssetType::AT_MATERIAL != item->getType())
    {
        // Not supported
        return;
    }

    if (isAssetInInventory(item, item->getType()))
    {
        // already there
        return;
    }

    mPendingInventoryItemsIDs.push_back(item->getAssetUUID());
    updateInventory(item, key, is_new);
}

void LLViewerObject::updateInventory(
    LLViewerInventoryItem* item,
    U8 key,
    bool is_new)
{
    // This slices the object into what we're concerned about on the
    // viewer. The simulator will take the permissions and transfer
    // ownership.
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(),
                                  item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_UpdateData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addU8Fast(_PREHASH_Key, key);
    msg->nextBlockFast(_PREHASH_InventoryData);
    task_item->packMessage(msg);
    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, key, is_new);
}

void LLViewerObject::updateInventoryLocal(LLInventoryItem* item, U8 key)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());

    // do the internal logic
    const bool is_new = false;
    doUpdateInventory(task_item, key, is_new);
}

LLInventoryObject* LLViewerObject::getInventoryObject(const LLUUID& item_id)
{
    LLInventoryObject* rv = NULL;
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for ( ; it != end; ++it)
        {
            if((*it)->getUUID() == item_id)
            {
                rv = *it;
                break;
            }
        }
    }
    return rv;
}

LLInventoryItem* LLViewerObject::getInventoryItem(const LLUUID& item_id)
{
    LLInventoryObject* iobj = getInventoryObject(item_id);
    if (!iobj || iobj->getType() == LLAssetType::AT_CATEGORY)
    {
        return NULL;
    }
    LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(iobj);
    return item;
}

void LLViewerObject::getInventoryContents(LLInventoryObject::object_list_t& objects)
{
    if(mInventory)
    {
        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it)
        {
            if ((*it)->getType() != LLAssetType::AT_CATEGORY)
            {
                objects.push_back(*it);
            }
        }
    }
}

LLInventoryObject* LLViewerObject::getInventoryRoot()
{
    if (!mInventory || !mInventory->size())
    {
        return NULL;
    }
    return mInventory->back();
}

LLViewerInventoryItem* LLViewerObject::getInventoryItemByAsset(const LLUUID& asset_id)
{
    if (mInventoryDirty)
        LL_WARNS() << "Peforming inventory lookup for object " << mID << " that has dirty inventory!" << LL_ENDL;

    LLViewerInventoryItem* rv = NULL;
    if(mInventory)
    {
        LLViewerInventoryItem* item = NULL;

        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for( ; it != end; ++it)
        {
            LLInventoryObject* obj = *it;
            if(obj->getType() != LLAssetType::AT_CATEGORY)
            {
                // *FIX: gank-ass down cast!
                item = (LLViewerInventoryItem*)obj;
                if(item->getAssetUUID() == asset_id)
                {
                    rv = item;
                    break;
                }
            }
        }
    }
    return rv;
}

LLViewerInventoryItem* LLViewerObject::getInventoryItemByAsset(const LLUUID& asset_id, LLAssetType::EType type)
{
    if (mInventoryDirty)
        LL_WARNS() << "Peforming inventory lookup for object " << mID << " that has dirty inventory!" << LL_ENDL;

    LLViewerInventoryItem* rv = NULL;
    if (type == LLAssetType::AT_CATEGORY)
    {
        // Whatever called this shouldn't be trying to get a folder by asset
        // categories don't have assets
        llassert(0);
        return rv;
    }

    if (mInventory)
    {
        LLViewerInventoryItem* item = NULL;

        LLInventoryObject::object_list_t::iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::iterator end = mInventory->end();
        for (; it != end; ++it)
        {
            LLInventoryObject* obj = *it;
            if (obj->getType() == type)
            {
                // *FIX: gank-ass down cast!
                item = (LLViewerInventoryItem*)obj;
                if (item->getAssetUUID() == asset_id)
                {
                    rv = item;
                    break;
                }
            }
        }
    }
    return rv;
}

void LLViewerObject::updateViewerInventoryAsset(
                    const LLViewerInventoryItem* item,
                    const LLUUID& new_asset)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item);
    task_item->setAssetUUID(new_asset);

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, false);
}

void LLViewerObject::setPixelAreaAndAngle(LLAgent &agent)
{
    if (getVolume())
    {   //volumes calculate pixel area and angle per face
        return;
    }

    LLVector3 viewer_pos_agent = gAgentCamera.getCameraPositionAgent();
    LLVector3 pos_agent = getRenderPosition();

    F32 dx = viewer_pos_agent.mV[VX] - pos_agent.mV[VX];
    F32 dy = viewer_pos_agent.mV[VY] - pos_agent.mV[VY];
    F32 dz = viewer_pos_agent.mV[VZ] - pos_agent.mV[VZ];

    F32 max_scale = getMaxScale();
    F32 mid_scale = getMidScale();
    F32 min_scale = getMinScale();

    // IW: estimate - when close to large objects, computing range based on distance from center is no good
    // to try to get a min distance from face, subtract min_scale/2 from the range.
    // This means we'll load too much detail sometimes, but that's better than not enough
    // I don't think there's a better way to do this without calculating distance per-poly
    F32 range = sqrt(dx*dx + dy*dy + dz*dz) - min_scale/2;

    LLViewerCamera* camera = LLViewerCamera::getInstance();
    if (range < 0.001f || isHUDAttachment())        // range == zero
    {
        mAppAngle = 180.f;
        mPixelArea = (F32)camera->getScreenPixelArea();
    }
    else
    {
        mAppAngle = (F32) atan2( max_scale, range) * RAD_TO_DEG;

        F32 pixels_per_meter = camera->getPixelMeterRatio() / range;

        mPixelArea = (pixels_per_meter * max_scale) * (pixels_per_meter * mid_scale);
        if (mPixelArea > camera->getScreenPixelArea())
        {
            mAppAngle = 180.f;
            mPixelArea = (F32)camera->getScreenPixelArea();
        }
    }
}

bool LLViewerObject::updateLOD()
{
    return false;
}

bool LLViewerObject::updateGeometry(LLDrawable *drawable)
{
    // return true means "update complete", return false means "try again next frame"
    // default should be return true
    return true;
}

void LLViewerObject::updateGL()
{

}

void LLViewerObject::updateFaceSize(S32 idx)
{

}

LLDrawable* LLViewerObject::createDrawable(LLPipeline *pipeline)
{
    return NULL;
}

void LLViewerObject::setScale(const LLVector3 &scale, bool damped)
{
    LLPrimitive::setScale(scale);
    if (mDrawable.notNull())
    {
        //encompass completely sheared objects by taking
        //the most extreme point possible (<1,1,0.5>)
        mDrawable->setRadius(LLVector3(1,1,0.5f).scaleVec(scale).magVec());
        updateDrawable(damped);
    }

    if( (LL_PCODE_VOLUME == getPCode()) && !isDead() )
    {
        if (permYouOwner() || (scale.magVecSquared() > (7.5f * 7.5f)) )
        {
            if (!mOnMap)
            {
                llassert_always(LLWorld::getInstance()->getRegionFromHandle(getRegion()->getHandle()));

                gObjectList.addToMap(this);
                mOnMap = true;
            }
        }
        else
        {
            if (mOnMap)
            {
                gObjectList.removeFromMap(this);
                mOnMap = false;
            }
        }
    }
}

void LLViewerObject::setObjectCostStale()
{
    mCostStale = true;
    // *NOTE: This is harmlessly redundant for Blinn-Phong material updates, as
    // the root prim currently gets set stale anyway due to other property
    // updates. But it is needed for GLTF material ID updates.
    // -Cosmic,2023-06-27
    getRootEdit()->mCostStale = true;
}

void LLViewerObject::setObjectCost(F32 cost)
{
    mObjectCost = cost;
    mCostStale = false;

    if (isSelected() && gFloaterTools)
    {
        gFloaterTools->dirty();
    }
}

void LLViewerObject::setLinksetCost(F32 cost)
{
    mLinksetCost = cost;
    mCostStale = false;

    bool needs_refresh = isSelected();
    child_list_t::iterator iter = mChildList.begin();
    while(iter != mChildList.end() && !needs_refresh)
    {
        LLViewerObject* child = *iter;
        needs_refresh = child->isSelected();
        iter++;
    }

    if (needs_refresh && gFloaterTools)
    {
        gFloaterTools->dirty();
    }
}

void LLViewerObject::setPhysicsCost(F32 cost)
{
    mPhysicsCost = cost;
    mCostStale = false;

    if (isSelected() && gFloaterTools)
    {
        gFloaterTools->dirty();
    }
}

void LLViewerObject::setLinksetPhysicsCost(F32 cost)
{
    mLinksetPhysicsCost = cost;
    mCostStale = false;

    if (isSelected() && gFloaterTools)
    {
        gFloaterTools->dirty();
    }
}


F32 LLViewerObject::getObjectCost()
{
    if (mCostStale)
    {
        gObjectList.updateObjectCost(this);
    }

    return mObjectCost;
}

F32 LLViewerObject::getLinksetCost()
{
    if (mCostStale)
    {
        gObjectList.updateObjectCost(this);
    }

    return mLinksetCost;
}

F32 LLViewerObject::getPhysicsCost()
{
    if (mCostStale)
    {
        gObjectList.updateObjectCost(this);
    }

    return mPhysicsCost;
}

F32 LLViewerObject::getLinksetPhysicsCost()
{
    if (mCostStale)
    {
        gObjectList.updateObjectCost(this);
    }

    return mLinksetPhysicsCost;
}

F32 LLViewerObject::recursiveGetEstTrianglesMax() const
{
    F32 est_tris = getEstTrianglesMax();
    for (child_list_t::const_iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        const LLViewerObject* child = *iter;
        if (!child->isAvatar())
        {
            est_tris += child->recursiveGetEstTrianglesMax();
        }
    }
    return est_tris;
}

S32 LLViewerObject::getAnimatedObjectMaxTris() const
{
    S32 max_tris = 0;
        if (gAgent.getRegion())
        {
            LLSD features;
            gAgent.getRegion()->getSimulatorFeatures(features);
            if (features.has("AnimatedObjects"))
            {
                max_tris = features["AnimatedObjects"]["AnimatedObjectMaxTris"].asInteger();
            }
        }
    return max_tris;
}

F32 LLViewerObject::getEstTrianglesMax() const
{
    return 0.f;
}

F32 LLViewerObject::getEstTrianglesStreamingCost() const
{
    return 0.f;
}

// virtual
F32 LLViewerObject::getStreamingCost() const
{
    return 0.f;
}

// virtual
bool LLViewerObject::getCostData(LLMeshCostData& costs) const
{
    costs = LLMeshCostData();
    return false;
}

U32 LLViewerObject::getTriangleCount(S32* vcount) const
{
    return 0;
}

U32 LLViewerObject::getHighLODTriangleCount()
{
    return 0;
}

U32 LLViewerObject::recursiveGetTriangleCount(S32* vcount) const
{
    S32 total_tris = getTriangleCount(vcount);
    LLViewerObject::const_child_list_t& child_list = getChildren();
    for (LLViewerObject::const_child_list_t::const_iterator iter = child_list.begin();
         iter != child_list.end(); ++iter)
    {
        LLViewerObject* childp = *iter;
        if (childp)
        {
            total_tris += childp->getTriangleCount(vcount);
        }
    }
    return total_tris;
}

// This is using the stored surface area for each volume (which
// defaults to 1.0 for the case of everything except a sculpt) and
// then scaling it linearly based on the largest dimension in the
// prim's scale. Should revisit at some point.
F32 LLViewerObject::recursiveGetScaledSurfaceArea() const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    F32 area = 0.f;
    const LLDrawable* drawable = mDrawable;
    if (drawable)
    {
        const LLVOVolume* volume = drawable->getVOVolume();
        if (volume)
        {
            if (volume->getVolume())
            {
                const LLVector3& scale = volume->getScale();
                area += volume->getVolume()->getSurfaceArea() * llmax(llmax(scale.mV[0], scale.mV[1]), scale.mV[2]);
            }
            LLViewerObject::const_child_list_t children = volume->getChildren();
            for (LLViewerObject::const_child_list_t::const_iterator child_iter = children.begin();
                 child_iter != children.end();
                 ++child_iter)
            {
                LLViewerObject* child_obj = *child_iter;
                LLVOVolume *child = dynamic_cast<LLVOVolume*>( child_obj );
                if (child && child->getVolume())
                {
                    const LLVector3& scale = child->getScale();
                    area += child->getVolume()->getSurfaceArea() * llmax(llmax(scale.mV[0], scale.mV[1]), scale.mV[2]);
                }
            }
        }
    }
    return area;
}

void LLViewerObject::updateSpatialExtents(LLVector4a& newMin, LLVector4a &newMax)
{
    LLVector4a center;
    center.load3(getRenderPosition().mV);
    LLVector4a size;
    size.load3(getScale().mV);
    newMin.setSub(center, size);
    newMax.setAdd(center, size);

    mDrawable->setPositionGroup(center);
}

F32 LLViewerObject::getBinRadius()
{
    if (mDrawable.notNull())
    {
        const LLVector4a* ext = mDrawable->getSpatialExtents();
        LLVector4a diff;
        diff.setSub(ext[1], ext[0]);
        return diff.getLength3().getF32();
    }

    return getScale().magVec();
}

F32 LLViewerObject::getMaxScale() const
{
    return llmax(getScale().mV[VX],getScale().mV[VY], getScale().mV[VZ]);
}

F32 LLViewerObject::getMinScale() const
{
    return llmin(getScale().mV[0],getScale().mV[1],getScale().mV[2]);
}

F32 LLViewerObject::getMidScale() const
{
    if (getScale().mV[VX] < getScale().mV[VY])
    {
        if (getScale().mV[VY] < getScale().mV[VZ])
        {
            return getScale().mV[VY];
        }
        else if (getScale().mV[VX] < getScale().mV[VZ])
        {
            return getScale().mV[VZ];
        }
        else
        {
            return getScale().mV[VX];
        }
    }
    else if (getScale().mV[VX] < getScale().mV[VZ])
    {
        return getScale().mV[VX];
    }
    else if (getScale().mV[VY] < getScale().mV[VZ])
    {
        return getScale().mV[VZ];
    }
    else
    {
        return getScale().mV[VY];
    }
}


void LLViewerObject::updateTextures()
{
}

void LLViewerObject::boostTexturePriority(bool boost_children /* = true */)
{
    if (isDead() || !getVolume())
    {
        return;
    }

    S32 i;
    S32 tex_count = getNumTEs();
    for (i = 0; i < tex_count; i++)
    {
        getTEImage(i)->setBoostLevel(LLGLTexture::BOOST_SELECTED);
    }

    if (isSculpted() && !isMesh())
    {
        LLSculptParams *sculpt_params = (LLSculptParams *)getParameterEntry(LLNetworkData::PARAMS_SCULPT);
        LLUUID sculpt_id = sculpt_params->getSculptTexture();
        LLViewerTextureManager::getFetchedTexture(sculpt_id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE)->setBoostLevel(LLGLTexture::BOOST_SELECTED);
    }

    if (boost_children)
    {
        for (child_list_t::iterator iter = mChildList.begin();
             iter != mChildList.end(); iter++)
        {
            LLViewerObject* child = *iter;
            child->boostTexturePriority();
        }
    }
}

void LLViewerObject::setLineWidthForWindowSize(S32 window_width)
{
    if (window_width < 700)
    {
        LLUI::setLineWidth(2.0f);
    }
    else if (window_width < 1100)
    {
        LLUI::setLineWidth(3.0f);
    }
    else if (window_width < 2000)
    {
        LLUI::setLineWidth(4.0f);
    }
    else
    {
        // _damn_, what a nice monitor!
        LLUI::setLineWidth(5.0f);
    }
}

void LLViewerObject::increaseArrowLength()
{
/* ???
    if (mAxisArrowLength == 50)
    {
        mAxisArrowLength = 100;
    }
    else
    {
        mAxisArrowLength = 150;
    }
*/
}


void LLViewerObject::decreaseArrowLength()
{
/* ???
    if (mAxisArrowLength == 150)
    {
        mAxisArrowLength = 100;
    }
    else
    {
        mAxisArrowLength = 50;
    }
*/
}

// Culled from newsim LLTask::addNVPair
void LLViewerObject::addNVPair(const std::string& data)
{
    // cout << "LLViewerObject::addNVPair() with ---" << data << "---" << endl;
    LLNameValue *nv = new LLNameValue(data.c_str());

//  char splat[MAX_STRING];
//  temp->printNameValue(splat);
//  LL_INFOS() << "addNVPair " << splat << LL_ENDL;

    name_value_map_t::iterator iter = mNameValuePairs.find(nv->mName);
    if (iter != mNameValuePairs.end())
    {
        LLNameValue* foundnv = iter->second;
        if (foundnv->mClass != NVC_READ_ONLY)
        {
            delete foundnv;
            mNameValuePairs.erase(iter);
        }
        else
        {
            delete nv;
//          LL_INFOS() << "Trying to write to Read Only NVPair " << temp->mName << " in addNVPair()" << LL_ENDL;
            return;
        }
    }
    mNameValuePairs[nv->mName] = nv;
}

bool LLViewerObject::removeNVPair(const std::string& name)
{
    char* canonical_name = gNVNameTable.addString(name);

    LL_DEBUGS() << "LLViewerObject::removeNVPair(): " << name << LL_ENDL;

    name_value_map_t::iterator iter = mNameValuePairs.find(canonical_name);
    if (iter != mNameValuePairs.end())
    {
        if( mRegionp )
        {
            LLNameValue* nv = iter->second;
/*
            std::string buffer = nv->printNameValue();
            gMessageSystem->newMessageFast(_PREHASH_RemoveNameValuePair);
            gMessageSystem->nextBlockFast(_PREHASH_TaskData);
            gMessageSystem->addUUIDFast(_PREHASH_ID, mID);

            gMessageSystem->nextBlockFast(_PREHASH_NameValueData);
            gMessageSystem->addStringFast(_PREHASH_NVPair, buffer);

            gMessageSystem->sendReliable( mRegionp->getHost() );
*/
            // Remove the NV pair from the local list.
            delete nv;
            mNameValuePairs.erase(iter);
            return true;
        }
        else
        {
            LL_DEBUGS() << "removeNVPair - No region for object" << LL_ENDL;
        }
    }
    return false;
}


LLNameValue *LLViewerObject::getNVPair(const std::string& name) const
{
    char        *canonical_name;

    canonical_name = gNVNameTable.addString(name);

    // If you access a map with a name that isn't in it, it will add the name and a null pointer.
    // So first check if the data is in the map.
    name_value_map_t::const_iterator iter = mNameValuePairs.find(canonical_name);
    if (iter != mNameValuePairs.end())
    {
        return iter->second;
    }
    else
    {
        return NULL;
    }
}

void LLViewerObject::updatePositionCaches() const
{
    // If region is removed from the list it is also deleted.
    if(mRegionp && LLWorld::instance().isRegionListed(mRegionp))
    {
        if (!isRoot())
        {
            mPositionRegion = ((LLViewerObject *)getParent())->getPositionRegion() + getPosition() * getParent()->getRotation();
            mPositionAgent = mRegionp->getPosAgentFromRegion(mPositionRegion);
        }
        else
        {
            mPositionRegion = getPosition();
            mPositionAgent = mRegionp->getPosAgentFromRegion(mPositionRegion);
        }
    }
}

const LLVector3d LLViewerObject::getPositionGlobal() const
{
    // If region is removed from the list it is also deleted.
    if(mRegionp && LLWorld::instance().isRegionListed(mRegionp))
    {
        LLVector3d position_global = mRegionp->getPosGlobalFromRegion(getPositionRegion());

        if (isAttachment())
        {
            position_global = gAgent.getPosGlobalFromAgent(getRenderPosition());
        }
        return position_global;
    }
    else
    {
        LLVector3d position_global(getPosition());
        return position_global;
    }
}

const LLVector3 &LLViewerObject::getPositionAgent() const
{
    // If region is removed from the list it is also deleted.
    if(mRegionp && LLWorld::instance().isRegionListed(mRegionp))
    {
        if (mDrawable.notNull() && (!mDrawable->isRoot() && getParent()))
        {
            // Don't return cached position if you have a parent, recalc (until all dirtying is done correctly.
            LLVector3 position_region;
            position_region = ((LLViewerObject *)getParent())->getPositionRegion() + getPosition() * getParent()->getRotation();
            mPositionAgent = mRegionp->getPosAgentFromRegion(position_region);
        }
        else
        {
            mPositionAgent = mRegionp->getPosAgentFromRegion(getPosition());
        }
    }
    return mPositionAgent;
}

LLMatrix4a LLViewerObject::getGLTFAssetToAgentTransform() const
{
    LLMatrix4 root;
    root.initScale(getScale());
    root.rotate(getRenderRotation());
    root.translate(getRenderPosition());

    LLMatrix4a mat;
    mat.loadu((F32*)root.mMatrix);

    return mat;
}

LLVector3 LLViewerObject::getGLTFNodePositionAgent(S32 node_index) const
{
    LLVector3 ret;
    getGLTFNodeTransformAgent(node_index, &ret, nullptr, nullptr);
    return ret;

}

LLMatrix4a LLViewerObject::getAgentToGLTFAssetTransform() const
{
    LLMatrix4 root;
    LLVector3 scale = getScale();
    scale.mV[0] = 1.f / scale.mV[0];
    scale.mV[1] = 1.f / scale.mV[1];
    scale.mV[2] = 1.f / scale.mV[2];

    root.translate(-getRenderPosition());
    root.rotate(~getRenderRotation());

    LLMatrix4 scale_mat;
    scale_mat.initScale(scale);

    root *= scale_mat;
    LLMatrix4a mat;
    mat.loadu((F32*)root.mMatrix);

    return mat;
}

LLMatrix4a LLViewerObject::getGLTFNodeTransformAgent(S32 node_index) const
{
    LLMatrix4a mat;

    if (mGLTFAsset && node_index >= 0 && node_index < mGLTFAsset->mNodes.size())
    {
        auto& node = mGLTFAsset->mNodes[node_index];

        LLMatrix4a asset_to_agent = getGLTFAssetToAgentTransform();
        LLMatrix4a node_to_agent;
        LLMatrix4a am;
        am.loadu(glm::value_ptr(node.mAssetMatrix));
        matMul(am, asset_to_agent, node_to_agent);

        mat = node_to_agent;
    }
    else
    {
        mat.setIdentity();
    }

    return mat;
}

void LLViewerObject::getGLTFNodeTransformAgent(S32 node_index, LLVector3* position, LLQuaternion* rotation, LLVector3* scale) const
{
    LLMatrix4a node_to_agent = getGLTFNodeTransformAgent(node_index);

    if (position)
    {
        LLVector4a p = node_to_agent.getTranslation();
        position->set(p.getF32ptr());
    }

    if (rotation)
    {
        rotation->set(node_to_agent.asMatrix4());
    }

    if (scale)
    {
        scale->mV[0] = node_to_agent.mMatrix[0].getLength3().getF32();
        scale->mV[1] = node_to_agent.mMatrix[1].getLength3().getF32();
        scale->mV[2] = node_to_agent.mMatrix[2].getLength3().getF32();
    }
}

void decomposeMatrix(const LLMatrix4a& mat, LLVector3& position, LLQuaternion& rotation, LLVector3& scale)
{
    LLVector4a p = mat.getTranslation();
    position.set(p.getF32ptr());

    rotation.set(mat.asMatrix4());

    scale.mV[0] = mat.mMatrix[0].getLength3().getF32();
    scale.mV[1] = mat.mMatrix[1].getLength3().getF32();
    scale.mV[2] = mat.mMatrix[2].getLength3().getF32();
}

void LLViewerObject::setGLTFNodeRotationAgent(S32 node_index, const LLQuaternion& rotation)
{
    if (mGLTFAsset && node_index >= 0 && node_index < mGLTFAsset->mNodes.size())
    {
        auto& node = mGLTFAsset->mNodes[node_index];

        LLMatrix4a agent_to_asset = getAgentToGLTFAssetTransform();
        LLMatrix4a agent_to_node = agent_to_asset;

        if (node.mParent != -1)
        {
            auto& parent = mGLTFAsset->mNodes[node.mParent];
            LLMatrix4a ami;
            ami.loadu(glm::value_ptr(parent.mAssetMatrixInv));
            matMul(agent_to_asset, ami, agent_to_node);
        }

        LLQuaternion agent_to_node_rot(agent_to_node.asMatrix4());
        LLQuaternion new_rot;

        new_rot = rotation * agent_to_node_rot;
        new_rot.normalize();

        LLVector3 pos;
        LLQuaternion rot;
        LLVector3 scale;
        LLMatrix4a mat;
        mat.loadu(glm::value_ptr(node.mMatrix));
        decomposeMatrix(mat, pos, rot, scale);

        mat.asMatrix4().initAll(scale, new_rot, pos);

        node.mMatrix = glm::make_mat4(mat.getF32ptr());

        mGLTFAsset->updateTransforms();
    }
}

void LLViewerObject::moveGLTFNode(S32 node_index, const LLVector3& offset)
{
    if (mGLTFAsset && node_index >= 0 && node_index < mGLTFAsset->mNodes.size())
    {
        auto& node = mGLTFAsset->mNodes[node_index];

        LLMatrix4a agent_to_asset = getAgentToGLTFAssetTransform();
        LLMatrix4a agent_to_node;
        LLMatrix4a ami;
        ami.loadu(glm::value_ptr(node.mAssetMatrixInv));
        matMul(agent_to_asset, ami, agent_to_node);

        LLVector4a origin = LLVector4a::getZero();
        LLVector4a offset_v;
        offset_v.load3(offset.mV);


        agent_to_node.affineTransform(offset_v, offset_v);
        agent_to_node.affineTransform(origin, origin);

        offset_v.sub(origin);
        offset_v.getF32ptr()[3] = 1.f;

        LLMatrix4a trans;
        trans.setIdentity();
        trans.mMatrix[3] = offset_v;

        LLMatrix4a mat;
        mat.loadu(glm::value_ptr(node.mMatrix));

        matMul(trans, mat, mat);

        node.mMatrix = glm::make_mat4(mat.getF32ptr());
        node.mTRSValid = false;

        // TODO -- only update transforms for this node and its children (or use a dirty flag)
        mGLTFAsset->updateTransforms();
    }
}

const LLVector3 &LLViewerObject::getPositionRegion() const
{
    if (!isRoot())
    {
        LLViewerObject *parent = (LLViewerObject *)getParent();
        mPositionRegion = parent->getPositionRegion() + (getPosition() * parent->getRotation());
    }
    else
    {
        mPositionRegion = getPosition();
    }

    return mPositionRegion;
}

const LLVector3 LLViewerObject::getPositionEdit() const
{
    if (isRootEdit())
    {
        return getPosition();
    }
    else
    {
        LLViewerObject *parent = (LLViewerObject *)getParent();
        LLVector3 position_edit = parent->getPositionEdit() + getPosition() * parent->getRotationEdit();
        return position_edit;
    }
}

const LLVector3 LLViewerObject::getRenderPosition() const
{
    if (mDrawable.notNull() && mDrawable->isState(LLDrawable::RIGGED))
    {
        LLControlAvatar *cav = getControlAvatar();
        if (isRoot() && cav)
        {
            F32 fixup;
            if ( cav->hasPelvisFixup( fixup) )
            {
                //Apply a pelvis fixup (as defined by the avs skin)
                LLVector3 pos = mDrawable->getPositionAgent();
                pos[VZ] += fixup;
                return pos;
            }
        }
        LLVOAvatar* avatar = getAvatar();
        if ((avatar) && !getControlAvatar())
        {
            return avatar->getPositionAgent();
        }
    }

    if (mDrawable.isNull() || mDrawable->getGeneration() < 0)
    {
        return getPositionAgent();
    }
    else
    {
        return mDrawable->getPositionAgent();
    }
}

const LLVector3 LLViewerObject::getPivotPositionAgent() const
{
    return getRenderPosition();
}

const LLQuaternion LLViewerObject::getRenderRotation() const
{
    LLQuaternion ret;
    if (mDrawable.notNull() && mDrawable->isState(LLDrawable::RIGGED) && !isAnimatedObject())
    {
        return ret;
    }

    if (mDrawable.isNull() || mDrawable->isStatic())
    {
        ret = getRotationEdit();
    }
    else
    {
        if (!mDrawable->isRoot())
        {
            ret = getRotation() * LLQuaternion(mDrawable->getParent()->getWorldMatrix());
        }
        else
        {
            ret = LLQuaternion(mDrawable->getWorldMatrix());
        }
    }

    return ret;
}

const LLMatrix4 LLViewerObject::getRenderMatrix() const
{
    return mDrawable->getWorldMatrix();
}

const LLQuaternion LLViewerObject::getRotationRegion() const
{
    LLQuaternion global_rotation = getRotation();
    if (!((LLXform *)this)->isRoot())
    {
        global_rotation = global_rotation * getParent()->getRotation();
    }
    return global_rotation;
}

const LLQuaternion LLViewerObject::getRotationEdit() const
{
    LLQuaternion global_rotation = getRotation();
    if (!((LLXform *)this)->isRootEdit())
    {
        global_rotation = global_rotation * getParent()->getRotation();
    }
    return global_rotation;
}

void LLViewerObject::setPositionAbsoluteGlobal( const LLVector3d &pos_global, bool damped )
{
    if (isAttachment())
    {
        LLVector3 new_pos = mRegionp->getPosRegionFromGlobal(pos_global);
        if (isRootEdit())
        {
            new_pos -= mDrawable->mXform.getParent()->getWorldPosition();
            LLQuaternion world_rotation = mDrawable->mXform.getParent()->getWorldRotation();
            new_pos = new_pos * ~world_rotation;
        }
        else
        {
            LLViewerObject* parentp = (LLViewerObject*)getParent();
            new_pos -= parentp->getPositionAgent();
            new_pos = new_pos * ~parentp->getRotationRegion();
        }
        LLViewerObject::setPosition(new_pos);

        if (mParent && ((LLViewerObject*)mParent)->isAvatar())
        {
            // we have changed the position of an attachment, so we need to clamp it
            LLVOAvatar *avatar = (LLVOAvatar*)mParent;

            avatar->clampAttachmentPositions();
        }
    }
    else
    {
        if( isRoot() )
        {
            setPositionRegion(mRegionp->getPosRegionFromGlobal(pos_global));
        }
        else
        {
            // the relative position with the parent is not constant
            LLViewerObject* parent = (LLViewerObject *)getParent();
            //RN: this assumes we are only calling this function from the edit tools
            gPipeline.updateMoveNormalAsync(parent->mDrawable);

            LLVector3 pos_local = mRegionp->getPosRegionFromGlobal(pos_global) - parent->getPositionRegion();
            pos_local = pos_local * ~parent->getRotationRegion();
            LLViewerObject::setPosition( pos_local );
        }
    }
    //RN: assumes we always want to snap the object when calling this function
    gPipeline.updateMoveNormalAsync(mDrawable);
}

void LLViewerObject::setPosition(const LLVector3 &pos, bool damped)
{
    if (getPosition() != pos)
    {
        setChanged(TRANSLATED | SILHOUETTE);
    }

    LLXform::setPosition(pos);
    updateDrawable(damped);
    if (isRoot())
    {
        // position caches need to be up to date on root objects
        updatePositionCaches();
    }
}

void LLViewerObject::setPositionGlobal(const LLVector3d &pos_global, bool damped)
{
    if (isAttachment())
    {
        if (isRootEdit())
        {
            LLVector3 newPos = mRegionp->getPosRegionFromGlobal(pos_global);
            newPos = newPos - mDrawable->mXform.getParent()->getWorldPosition();

            LLQuaternion invWorldRotation = mDrawable->mXform.getParent()->getWorldRotation();
            invWorldRotation.transQuat();

            newPos = newPos * invWorldRotation;
            LLViewerObject::setPosition(newPos);
        }
        else
        {
            // assumes parent is root editable (root of attachment)
            LLVector3 newPos = mRegionp->getPosRegionFromGlobal(pos_global);
            newPos = newPos - mDrawable->mXform.getParent()->getWorldPosition();
            LLVector3 delta_pos = newPos - getPosition();

            LLQuaternion invRotation = mDrawable->getRotation();
            invRotation.transQuat();

            delta_pos = delta_pos * invRotation;

            // *FIX: is this right?  Shouldn't we be calling the
            // LLViewerObject version of setPosition?
            LLVector3 old_pos = mDrawable->mXform.getParent()->getPosition();
            mDrawable->mXform.getParent()->setPosition(old_pos + delta_pos);
            setChanged(TRANSLATED | SILHOUETTE);
        }
        if (mParent && ((LLViewerObject*)mParent)->isAvatar())
        {
            // we have changed the position of an attachment, so we need to clamp it
            LLVOAvatar *avatar = (LLVOAvatar*)mParent;

            avatar->clampAttachmentPositions();
        }
    }
    else
    {
        if (isRoot())
        {
            setPositionRegion(mRegionp->getPosRegionFromGlobal(pos_global));
        }
        else
        {
            // the relative position with the parent is constant, but the parent's position needs to be changed
            LLVector3d position_offset;
            position_offset.setVec(getPosition()*getParent()->getRotation());
            LLVector3d new_pos_global = pos_global - position_offset;
            ((LLViewerObject *)getParent())->setPositionGlobal(new_pos_global);
        }
    }
    updateDrawable(damped);
}


void LLViewerObject::setPositionParent(const LLVector3 &pos_parent, bool damped)
{
    // Set position relative to parent, if no parent, relative to region
    if (!isRoot())
    {
        LLViewerObject::setPosition(pos_parent, damped);
        //updateDrawable(damped);
    }
    else
    {
        setPositionRegion(pos_parent, damped);

        // #1964 mark reflection probe in the linkset to update position after moving via script
        for (LLViewerObject* child : mChildList)
        {
            if (child && child->isReflectionProbe())
            {
                if (LLDrawable* drawablep = child->mDrawable)
                {
                    gPipeline.markMoved(drawablep);
                }
            }
        }
    }
}

void LLViewerObject::setPositionRegion(const LLVector3 &pos_region, bool damped)
{
    if (!isRootEdit())
    {
        LLViewerObject* parent = (LLViewerObject*) getParent();
        LLViewerObject::setPosition((pos_region-parent->getPositionRegion())*~parent->getRotationRegion());
    }
    else
    {
        LLViewerObject::setPosition(pos_region);
        mPositionRegion = pos_region;
        mPositionAgent = mRegionp->getPosAgentFromRegion(mPositionRegion);
    }
}

void LLViewerObject::setPositionAgent(const LLVector3 &pos_agent, bool damped)
{
    LLVector3 pos_region = getRegion()->getPosRegionFromAgent(pos_agent);
    setPositionRegion(pos_region, damped);
}

// identical to setPositionRegion() except it checks for child-joints
// and doesn't also move the joint-parent
// TODO -- implement similar intelligence for joint-parents toward
// their joint-children
void LLViewerObject::setPositionEdit(const LLVector3 &pos_edit, bool damped)
{
    if (!isRootEdit())
    {
        // the relative position with the parent is constant, but the parent's position needs to be changed
        LLVector3 position_offset = getPosition() * getParent()->getRotation();

        ((LLViewerObject *)getParent())->setPositionEdit(pos_edit - position_offset);
        updateDrawable(damped);
    }
    else
    {
        LLViewerObject::setPosition(pos_edit, damped);
        mPositionRegion = pos_edit;
        mPositionAgent = mRegionp->getPosAgentFromRegion(mPositionRegion);
    }
}


LLViewerObject* LLViewerObject::getRootEdit() const
{
    const LLViewerObject* root = this;
    while (root->mParent
           && !((LLViewerObject*)root->mParent)->isAvatar())
    {
        root = (LLViewerObject*)root->mParent;
    }
    return (LLViewerObject*)root;
}


bool LLViewerObject::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
                                          S32 face,
                                          bool pick_transparent,
                                          bool pick_rigged,
                                          bool pick_unselectable,
                                          S32* face_hit,
                                          LLVector4a* intersection,
                                          LLVector2* tex_coord,
                                          LLVector4a* normal,
                                          LLVector4a* tangent)
{
    return false;
}

bool LLViewerObject::lineSegmentBoundingBox(const LLVector4a& start, const LLVector4a& end)
{
    if (mDrawable.isNull() || mDrawable->isDead())
    {
        return false;
    }

    const LLVector4a* ext = mDrawable->getSpatialExtents();

    //VECTORIZE THIS
    LLVector4a center;
    center.setAdd(ext[1], ext[0]);
    center.mul(0.5f);
    LLVector4a size;
    size.setSub(ext[1], ext[0]);
    size.mul(0.5f);

    return LLLineSegmentBoxIntersect(start, end, center, size);
}

U8 LLViewerObject::getMediaType() const
{
    if (mMedia)
    {
        return mMedia->mMediaType;
    }
    else
    {
        return LLViewerObject::MEDIA_NONE;
    }
}

void LLViewerObject::setMediaType(U8 media_type)
{
    if (!mMedia)
    {
        // TODO what if we don't have a media pointer?
    }
    else if (mMedia->mMediaType != media_type)
    {
        mMedia->mMediaType = media_type;

        // TODO: update materials with new image
    }
}

std::string LLViewerObject::getMediaURL() const
{
    if (mMedia)
    {
        return mMedia->mMediaURL;
    }
    else
    {
        return std::string();
    }
}

void LLViewerObject::setMediaURL(const std::string& media_url)
{
    if (!mMedia)
    {
        mMedia = new LLViewerObjectMedia;
        mMedia->mMediaURL = media_url;
        mMedia->mPassedWhitelist = false;

        // TODO: update materials with new image
    }
    else if (mMedia->mMediaURL != media_url)
    {
        mMedia->mMediaURL = media_url;
        mMedia->mPassedWhitelist = false;

        // TODO: update materials with new image
    }
}

bool LLViewerObject::getMediaPassedWhitelist() const
{
    if (mMedia)
    {
        return mMedia->mPassedWhitelist;
    }
    else
    {
        return false;
    }
}

void LLViewerObject::setMediaPassedWhitelist(bool passed)
{
    if (mMedia)
    {
        mMedia->mPassedWhitelist = passed;
    }
}

bool LLViewerObject::setMaterial(const U8 material)
{
    bool res = LLPrimitive::setMaterial(material);
    if (res)
    {
        setChanged(TEXTURE);
    }
    return res;
}

void LLViewerObject::setNumTEs(const U8 num_tes)
{
    U32 i;
    if (num_tes != getNumTEs())
    {
        if (num_tes)
        {
            LLPointer<LLViewerTexture> *new_images;
            new_images = new LLPointer<LLViewerTexture>[num_tes];

            LLPointer<LLViewerTexture> *new_normmaps;
            new_normmaps = new LLPointer<LLViewerTexture>[num_tes];

            LLPointer<LLViewerTexture> *new_specmaps;
            new_specmaps = new LLPointer<LLViewerTexture>[num_tes];
            for (i = 0; i < num_tes; i++)
            {
                if (i < getNumTEs())
                {
                    new_images[i] = mTEImages[i];
                    new_normmaps[i] = mTENormalMaps[i];
                    new_specmaps[i] = mTESpecularMaps[i];
                }
                else if (getNumTEs())
                {
                    new_images[i] = mTEImages[getNumTEs()-1];
                    new_normmaps[i] = mTENormalMaps[getNumTEs()-1];
                    new_specmaps[i] = mTESpecularMaps[getNumTEs()-1];
                }
                else
                {
                    new_images[i] = NULL;
                    new_normmaps[i] = NULL;
                    new_specmaps[i] = NULL;
                }
            }

            deleteTEImages();

            mTEImages = new_images;
            mTENormalMaps = new_normmaps;
            mTESpecularMaps = new_specmaps;
        }
        else
        {
            deleteTEImages();
        }

        S32 original_tes = getNumTEs();

        LLPrimitive::setNumTEs(num_tes);
        setChanged(TEXTURE);

        // touch up GLTF materials
        if (original_tes > 0)
        {
            for (int i = original_tes; i < getNumTEs(); ++i)
            {
                LLTextureEntry* src = getTE(original_tes - 1);
                LLTextureEntry* tep = getTE(i);
                setRenderMaterialID(i, getRenderMaterialID(original_tes - 1), false);

                if (tep)
                {
                    LLGLTFMaterial* base_material = src->getGLTFMaterial();
                    LLGLTFMaterial* override_material = src->getGLTFMaterialOverride();
                    if (base_material && override_material)
                    {
                        tep->setGLTFMaterialOverride(new LLGLTFMaterial(*override_material));

                        LLGLTFMaterial* render_material = new LLFetchedGLTFMaterial();
                        *render_material = *base_material;
                        render_material->applyOverride(*override_material);
                        tep->setGLTFRenderMaterial(render_material);
                    }
                }
            }
        }

        if (mDrawable.notNull())
        {
            gPipeline.markTextured(mDrawable);
        }
    }
}

void LLViewerObject::sendMaterialUpdate() const
{
    LLViewerRegion* regionp = getRegion();
    if(!regionp) return;
    gMessageSystem->newMessageFast(_PREHASH_ObjectMaterial);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID,  mLocalID );
    gMessageSystem->addU8Fast(_PREHASH_Material, getMaterial() );
    gMessageSystem->sendReliable( regionp->getHost() );

}

//formerly send_object_shape(LLViewerObject *object)
void LLViewerObject::sendShapeUpdate()
{
    gMessageSystem->newMessageFast(_PREHASH_ObjectShape);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, mLocalID );

    LLVolumeMessage::packVolumeParams(&getVolume()->getParams(), gMessageSystem);

    LLViewerRegion *regionp = getRegion();
    gMessageSystem->sendReliable( regionp->getHost() );
}


void LLViewerObject::sendTEUpdate() const
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ObjectImage);

    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());

    msg->nextBlockFast(_PREHASH_ObjectData);
    msg->addU32Fast(_PREHASH_ObjectLocalID, mLocalID );
    if (mMedia)
    {
        msg->addString("MediaURL", mMedia->mMediaURL);
    }
    else
    {
        msg->addString("MediaURL", NULL);
    }

    // TODO send media type

    packTEMessage(msg);

    LLViewerRegion *regionp = getRegion();
    msg->sendReliable( regionp->getHost() );
}

LLViewerTexture* LLViewerObject::getBakedTextureForMagicId(const LLUUID& id)
{
    if (!LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
    {
        return NULL;
    }

    LLViewerObject *root = getRootEdit();
    if (root && root->isAnimatedObject())
    {
        return LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    }

    LLVOAvatar* avatar = getAvatar();
    if (avatar && !isHUDAttachment())
    {
        LLAvatarAppearanceDefines::EBakedTextureIndex texIndex = LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::assetIdToBakedTextureIndex(id);
        LLViewerTexture* bakedTexture = avatar->getBakedTexture(texIndex);
        if (bakedTexture == NULL || bakedTexture->isMissingAsset())
        {
            return LLViewerTextureManager::getFetchedTexture(IMG_DEFAULT, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
        }
        else
        {
            return bakedTexture;
        }
    }
    else
    {
        return LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    }

}

void LLViewerObject::updateAvatarMeshVisibility(const LLUUID& id, const LLUUID& old_id)
{
    if (id == old_id)
    {
        return;
    }

    if (!LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(old_id) && !LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
    {
        return;
    }

    LLVOAvatar* avatar = getAvatar();
    if (avatar)
    {
        avatar->updateMeshVisibility();
    }
}


void LLViewerObject::setTE(const U8 te, const LLTextureEntry& texture_entry)
{
    LLUUID old_image_id;
    if (getTE(te))
    {
        old_image_id = getTE(te)->getID();
    }

    LLPrimitive::setTE(te, texture_entry);

    const LLUUID& image_id = getTE(te)->getID();
    LLViewerTexture* bakedTexture = getBakedTextureForMagicId(image_id);
    mTEImages[te] = bakedTexture ? bakedTexture : LLViewerTextureManager::getFetchedTexture(image_id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);

    updateAvatarMeshVisibility(image_id, old_image_id);

    updateTEMaterialTextures(te);
}

void LLViewerObject::updateTEMaterialTextures(U8 te)
{
    if (getTE(te)->getMaterialParams().notNull())
    {
        const LLUUID& norm_id = getTE(te)->getMaterialParams()->getNormalID();
        mTENormalMaps[te] = LLViewerTextureManager::getFetchedTexture(norm_id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);

        const LLUUID& spec_id = getTE(te)->getMaterialParams()->getSpecularID();
        mTESpecularMaps[te] = LLViewerTextureManager::getFetchedTexture(spec_id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    }

    LLFetchedGLTFMaterial* mat = (LLFetchedGLTFMaterial*) getTE(te)->getGLTFRenderMaterial();
    llassert(mat == nullptr || dynamic_cast<LLFetchedGLTFMaterial*>(getTE(te)->getGLTFRenderMaterial()) != nullptr);
    LLUUID mat_id = getRenderMaterialID(te);
    if (mat == nullptr && mat_id.notNull())
    {
        mat = (LLFetchedGLTFMaterial*) gGLTFMaterialList.getMaterial(mat_id);
        llassert(mat == nullptr || dynamic_cast<LLFetchedGLTFMaterial*>(gGLTFMaterialList.getMaterial(mat_id)) != nullptr);
        if (mat->isFetching())
        { // material is not loaded yet, rebuild draw info when the object finishes loading
            mat->onMaterialComplete([id=getID()]
                {
                    LLViewerObject* obj = gObjectList.findObject(id);
                    if (obj)
                    {
                        obj->markForUpdate();
                    }
                });
        }
        getTE(te)->setGLTFMaterial(mat);
    }
    else if (mat_id.isNull() && mat != nullptr)
    {
        mat = nullptr;
        getTE(te)->setGLTFMaterial(nullptr);
    }

    auto fetch_texture = [this](const LLUUID& id)
    {
        LLViewerFetchedTexture* img = nullptr;
        if (id.notNull())
        {
            if (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
            {
                 // TODO -- fall back to LLTextureEntry::mGLTFRenderMaterial when overriding with baked texture
                LLViewerTexture* viewerTexture = getBakedTextureForMagicId(id);
                img = viewerTexture ? dynamic_cast<LLViewerFetchedTexture*>(viewerTexture) : nullptr;
            }
            else
            {
                img = LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
                img->addTextureStats(64.f * 64.f, true);
            }
        }

        return img;
    };

    if (mat != nullptr)
    {
        mat->mBaseColorTexture = fetch_texture(mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR]);
        mat->mNormalTexture = fetch_texture(mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL]);
        mat->mMetallicRoughnessTexture = fetch_texture(mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS]);
        mat->mEmissiveTexture= fetch_texture(mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE]);
    }
}

void LLViewerObject::refreshBakeTexture()
{
    for (int face_index = 0; face_index < getNumTEs(); face_index++)
    {
        LLTextureEntry* tex_entry = getTE(face_index);
        if (tex_entry && LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(tex_entry->getID()))
        {
            const LLUUID& image_id = tex_entry->getID();
            LLViewerTexture* bakedTexture = getBakedTextureForMagicId(image_id);
            changeTEImage(face_index, bakedTexture);
        }
    }
}

void LLViewerObject::setTEImage(const U8 te, LLViewerTexture *imagep)
{
    if (mTEImages[te] != imagep)
    {
        LLUUID old_image_id = getTE(te) ? getTE(te)->getID() : LLUUID::null;

        LLPrimitive::setTETexture(te, imagep->getID());

        LLViewerTexture* baked_texture = getBakedTextureForMagicId(imagep->getID());
        mTEImages[te] = baked_texture ? baked_texture : imagep;
        updateAvatarMeshVisibility(imagep->getID(), old_image_id);
        setChanged(TEXTURE);
        if (mDrawable.notNull())
        {
            gPipeline.markTextured(mDrawable);
        }
    }
}

S32 LLViewerObject::setTETextureCore(const U8 te, LLViewerTexture *image)
{
    LLUUID old_image_id = getTE(te)->getID();
    const LLUUID& uuid = image ? image->getID() : LLUUID::null;
    S32 retval = 0;
    if (uuid != getTE(te)->getID() ||
        uuid == LLUUID::null)
    {
        retval = LLPrimitive::setTETexture(te, uuid);
        LLViewerTexture* baked_texture = getBakedTextureForMagicId(uuid);
        mTEImages[te] = baked_texture ? baked_texture : image;
        updateAvatarMeshVisibility(uuid,old_image_id);
        setChanged(TEXTURE);
        if (mDrawable.notNull())
        {
            gPipeline.markTextured(mDrawable);
        }
    }
    return retval;
}

S32 LLViewerObject::setTENormalMapCore(const U8 te, LLViewerTexture *image)
{
    S32 retval = TEM_CHANGE_TEXTURE;
    const LLUUID& uuid = image ? image->getID() : LLUUID::null;
    if (uuid != getTE(te)->getID() ||
        uuid == LLUUID::null)
    {
        LLTextureEntry* tep = getTE(te);
        LLMaterial* mat = NULL;
        if (tep)
        {
           mat = tep->getMaterialParams();
        }

        if (mat)
        {
            mat->setNormalID(uuid);
        }
    }
    changeTENormalMap(te,image);
    return retval;
}

S32 LLViewerObject::setTESpecularMapCore(const U8 te, LLViewerTexture *image)
{
    S32 retval = TEM_CHANGE_TEXTURE;
    const LLUUID& uuid = image ? image->getID() : LLUUID::null;
    if (uuid != getTE(te)->getID() ||
        uuid == LLUUID::null)
    {
        LLTextureEntry* tep = getTE(te);
        LLMaterial* mat = NULL;
        if (tep)
        {
            mat = tep->getMaterialParams();
        }

        if (mat)
        {
            mat->setSpecularID(uuid);
        }
    }
    changeTESpecularMap(te, image);
    return retval;
}

//virtual
void LLViewerObject::changeTEImage(S32 index, LLViewerTexture* new_image)
{
    if(index < 0 || index >= getNumTEs())
    {
        return ;
    }
    mTEImages[index] = new_image ;
}

void LLViewerObject::changeTENormalMap(S32 index, LLViewerTexture* new_image)
{
    if(index < 0 || index >= getNumTEs())
    {
        return ;
    }
    mTENormalMaps[index] = new_image ;
    refreshMaterials();
}

void LLViewerObject::changeTESpecularMap(S32 index, LLViewerTexture* new_image)
{
    if(index < 0 || index >= getNumTEs())
    {
        return ;
    }
    mTESpecularMaps[index] = new_image ;
    refreshMaterials();
}

S32 LLViewerObject::setTETexture(const U8 te, const LLUUID& uuid)
{
    // Invalid host == get from the agent's sim
    LLViewerFetchedTexture *image = LLViewerTextureManager::getFetchedTexture(
        uuid, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE, 0, 0, LLHost());
        return setTETextureCore(te, image);
}

S32 LLViewerObject::setTENormalMap(const U8 te, const LLUUID& uuid)
{
    LLViewerFetchedTexture *image = (uuid == LLUUID::null) ? NULL : LLViewerTextureManager::getFetchedTexture(
        uuid, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE, 0, 0, LLHost());
    return setTENormalMapCore(te, image);
}

S32 LLViewerObject::setTESpecularMap(const U8 te, const LLUUID& uuid)
{
    LLViewerFetchedTexture *image = (uuid == LLUUID::null) ? NULL : LLViewerTextureManager::getFetchedTexture(
        uuid, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE, 0, 0, LLHost());
    return setTESpecularMapCore(te, image);
}

S32 LLViewerObject::setTEColor(const U8 te, const LLColor3& color)
{
    return setTEColor(te, LLColor4(color));
}

S32 LLViewerObject::setTEColor(const U8 te, const LLColor4& color)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (color != tep->getColor())
    {
        retval = LLPrimitive::setTEColor(te, color);
        if (mDrawable.notNull() && retval)
        {
            // These should only happen on updates which are not the initial update.
            dirtyMesh();
        }
    }
    return retval;
}

S32 LLViewerObject::setTEBumpmap(const U8 te, const U8 bump)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (bump != tep->getBumpmap())
    {
        retval = LLPrimitive::setTEBumpmap(te, bump);
        setChanged(TEXTURE);
        if (mDrawable.notNull() && retval)
        {
            gPipeline.markTextured(mDrawable);
            gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_GEOMETRY);
        }
    }
    return retval;
}

S32 LLViewerObject::setTETexGen(const U8 te, const U8 texgen)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (texgen != tep->getTexGen())
    {
        retval = LLPrimitive::setTETexGen(te, texgen);
        setChanged(TEXTURE);
    }
    return retval;
}

S32 LLViewerObject::setTEMediaTexGen(const U8 te, const U8 media)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (media != tep->getMediaTexGen())
    {
        retval = LLPrimitive::setTEMediaTexGen(te, media);
        setChanged(TEXTURE);
    }
    return retval;
}

S32 LLViewerObject::setTEShiny(const U8 te, const U8 shiny)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (shiny != tep->getShiny())
    {
        retval = LLPrimitive::setTEShiny(te, shiny);
        setChanged(TEXTURE);
    }
    return retval;
}

S32 LLViewerObject::setTEFullbright(const U8 te, const U8 fullbright)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (fullbright != tep->getFullbright())
    {
        retval = LLPrimitive::setTEFullbright(te, fullbright);
        setChanged(TEXTURE);
        if (mDrawable.notNull() && retval)
        {
            gPipeline.markTextured(mDrawable);
        }
    }
    return retval;
}

S32 LLViewerObject::setTEMediaFlags(const U8 te, const U8 media_flags)
{
    // this might need work for media type
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (media_flags != tep->getMediaFlags())
    {
        retval = LLPrimitive::setTEMediaFlags(te, media_flags);
        setChanged(TEXTURE);
        if (mDrawable.notNull() && retval)
        {
            gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
            gPipeline.markTextured(mDrawable);
        }
    }
    return retval;
}

S32 LLViewerObject::setTEGlow(const U8 te, const F32 glow)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
    }
    else if (glow != tep->getGlow())
    {
        retval = LLPrimitive::setTEGlow(te, glow);
        setChanged(TEXTURE);
        if (mDrawable.notNull() && retval)
        {
            gPipeline.markTextured(mDrawable);
        }
    }
    return retval;
}

S32 LLViewerObject::setTEMaterialID(const U8 te, const LLMaterialID& pMaterialID)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS("Material") << "No texture entry for te " << (S32)te
                             << ", object " << mID
                             << ", material " << pMaterialID
                             << LL_ENDL;
    }
    //else if (pMaterialID != tep->getMaterialID())
    {
        LL_DEBUGS("Material") << "Changing texture entry for te " << (S32)te
                             << ", object " << mID
                             << ", material " << pMaterialID
                             << LL_ENDL;
        retval = LLPrimitive::setTEMaterialID(te, pMaterialID);
        refreshMaterials();
    }
    return retval;
}

S32 LLViewerObject::setTEMaterialParams(const U8 te, const LLMaterialPtr pMaterialParams)
{
    S32 retval = 0;
    const LLTextureEntry *tep = getTE(te);
    if (!tep)
    {
        LL_WARNS() << "No texture entry for te " << (S32)te << ", object " << mID << LL_ENDL;
        return 0;
    }

    retval = LLPrimitive::setTEMaterialParams(te, pMaterialParams);
    LL_DEBUGS("Material") << "Changing material params for te " << (S32)te
                            << ", object " << mID
                           << " (" << retval << ")"
                            << LL_ENDL;
    setTENormalMap(te, (pMaterialParams) ? pMaterialParams->getNormalID() : LLUUID::null);
    setTESpecularMap(te, (pMaterialParams) ? pMaterialParams->getSpecularID() : LLUUID::null);

    return retval;
}

S32 LLViewerObject::setTEGLTFMaterialOverride(U8 te, LLGLTFMaterial* override_mat)
{
    LL_PROFILE_ZONE_SCOPED;
    S32 retval = TEM_CHANGE_NONE;

    LLTextureEntry* tep = getTE(te);
    if (!tep)
    { // this could happen if the object is not fully formed yet
        // returning TEM_CHANGE_NONE here signals to LLGLTFMaterialList to queue the override for later
        return retval;
    }

    LLFetchedGLTFMaterial* src_mat = (LLFetchedGLTFMaterial*) tep->getGLTFMaterial();
    llassert(src_mat == nullptr || dynamic_cast<LLFetchedGLTFMaterial*>(tep->getGLTFMaterial()) != nullptr);
    // if override mat exists, we must also have a source mat
    if (!src_mat)
    {
        // we can get into this state if an override has arrived before the viewer has
        // received or handled an update, return TEM_CHANGE_NONE to signal to LLGLTFMaterialList that it
        // should queue the update for later
        return retval;
    }

    if(src_mat->isFetching())
    {
        // if still fetching, we need to wait until it is done and try again
        return retval;
    }

    retval = tep->setGLTFMaterialOverride(override_mat);

    if (retval)
    {
        if (override_mat)
        {
            LLFetchedGLTFMaterial* render_mat = new LLFetchedGLTFMaterial(*src_mat);
            render_mat->applyOverride(*override_mat);
            tep->setGLTFRenderMaterial(render_mat);
            retval = TEM_CHANGE_TEXTURE;

            for (LLGLTFMaterial::local_tex_map_t::value_type &val : override_mat->mTrackingIdToLocalTexture)
            {
                LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(val.first, override_mat);
            }

        }
        else if (tep->setGLTFRenderMaterial(nullptr))
        {
            retval = TEM_CHANGE_TEXTURE;
        }
    }

    return retval;
}

void LLViewerObject::refreshMaterials()
{
    setChanged(TEXTURE);
    if (mDrawable.notNull())
    {
        gPipeline.markTextured(mDrawable);
    }
}

S32 LLViewerObject::setTEScale(const U8 te, const F32 s, const F32 t)
{
    S32 retval = 0;
    retval = LLPrimitive::setTEScale(te, s, t);
    setChanged(TEXTURE);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }
    return retval;
}

S32 LLViewerObject::setTEScaleS(const U8 te, const F32 s)
{
    S32 retval = LLPrimitive::setTEScaleS(te, s);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }

    return retval;
}

S32 LLViewerObject::setTEScaleT(const U8 te, const F32 t)
{
    S32 retval = LLPrimitive::setTEScaleT(te, t);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }

    return retval;
}

S32 LLViewerObject::setTEOffset(const U8 te, const F32 s, const F32 t)
{
    S32 retval = LLPrimitive::setTEOffset(te, s, t);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }
    return retval;
}

S32 LLViewerObject::setTEOffsetS(const U8 te, const F32 s)
{
    S32 retval = LLPrimitive::setTEOffsetS(te, s);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }

    return retval;
}

S32 LLViewerObject::setTEOffsetT(const U8 te, const F32 t)
{
    S32 retval = LLPrimitive::setTEOffsetT(te, t);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
    }

    return retval;
}

S32 LLViewerObject::setTERotation(const U8 te, const F32 r)
{
    S32 retval = LLPrimitive::setTERotation(te, r);
    if (mDrawable.notNull() && retval)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_TCOORD);
        shrinkWrap();
    }
    return retval;
}


LLViewerTexture *LLViewerObject::getTEImage(const U8 face) const
{
//  llassert(mTEImages);

    if (face < getNumTEs())
    {
        LLViewerTexture* image = mTEImages[face];
        if (image)
        {
            return image;
        }
        else
        {
            return (LLViewerTexture*)(LLViewerFetchedTexture::sDefaultImagep);
        }
    }

    LL_ERRS() << llformat("Requested Image from invalid face: %d/%d",face,getNumTEs()) << LL_ENDL;

    return NULL;
}


bool LLViewerObject::isImageAlphaBlended(const U8 te) const
{
    LLViewerTexture* image = getTEImage(te);
    LLGLenum format = image ? image->getPrimaryFormat() : GL_RGB;
    switch (format)
    {
        case GL_RGBA:
        case GL_ALPHA:
        {
            return true;
        }
        break;

        case GL_RGB: break;
        default:
        {
            LL_WARNS() << "Unexpected tex format in LLViewerObject::isImageAlphaBlended...returning no alpha." << LL_ENDL;
        }
        break;
    }

    return false;
}

LLViewerTexture *LLViewerObject::getTENormalMap(const U8 face) const
{
    //  llassert(mTEImages);

    if (face < getNumTEs())
    {
        LLViewerTexture* image = mTENormalMaps[face];
        if (image)
        {
            return image;
        }
        else
        {
            return (LLViewerTexture*)(LLViewerFetchedTexture::sDefaultImagep);
        }
    }

    LL_ERRS() << llformat("Requested Image from invalid face: %d/%d",face,getNumTEs()) << LL_ENDL;

    return NULL;
}

LLViewerTexture *LLViewerObject::getTESpecularMap(const U8 face) const
{
    //  llassert(mTEImages);

    if (face < getNumTEs())
    {
        LLViewerTexture* image = mTESpecularMaps[face];
        if (image)
        {
            return image;
        }
        else
        {
            return (LLViewerTexture*)(LLViewerFetchedTexture::sDefaultImagep);
        }
    }

    LL_ERRS() << llformat("Requested Image from invalid face: %d/%d",face,getNumTEs()) << LL_ENDL;

    return NULL;
}

void LLViewerObject::fitFaceTexture(const U8 face)
{
    LL_INFOS() << "fitFaceTexture not implemented" << LL_ENDL;
}

LLBBox LLViewerObject::getBoundingBoxAgent() const
{
    LLVector3 position_agent;
    LLQuaternion rot;
    LLViewerObject* avatar_parent = NULL;
    LLViewerObject* root_edit = (LLViewerObject*)getRootEdit();
    if (root_edit)
    {
        avatar_parent = (LLViewerObject*)root_edit->getParent();
    }

    if (avatar_parent && avatar_parent->isAvatar() &&
        root_edit && root_edit->mDrawable.notNull() && !root_edit->mDrawable->isDead() &&
        root_edit->mDrawable->getXform()->getParent())
    {
        LLXform* parent_xform = root_edit->mDrawable->getXform()->getParent();
        position_agent = (getPositionEdit() * parent_xform->getWorldRotation()) + parent_xform->getWorldPosition();
        rot = getRotationEdit() * parent_xform->getWorldRotation();
    }
    else
    {
        position_agent = getPositionAgent();
        rot = getRotationRegion();
    }

    return LLBBox( position_agent, rot, getScale() * -0.5f, getScale() * 0.5f );
}

U32 LLViewerObject::getNumVertices() const
{
    U32 num_vertices = 0;
    if (mDrawable.notNull())
    {
        S32 i, num_faces;
        num_faces = mDrawable->getNumFaces();
        for (i = 0; i < num_faces; i++)
        {
            LLFace * facep = mDrawable->getFace(i);
            if (facep)
            {
                num_vertices += facep->getGeomCount();
            }
        }
    }
    return num_vertices;
}

U32 LLViewerObject::getNumIndices() const
{
    U32 num_indices = 0;
    if (mDrawable.notNull())
    {
        S32 i, num_faces;
        num_faces = mDrawable->getNumFaces();
        for (i = 0; i < num_faces; i++)
        {
            LLFace * facep = mDrawable->getFace(i);
            if (facep)
            {
                num_indices += facep->getIndicesCount();
            }
        }
    }
    return num_indices;
}

// Find the number of instances of this object's inventory that are of the given type
S32 LLViewerObject::countInventoryContents(LLAssetType::EType type)
{
    S32 count = 0;
    if( mInventory )
    {
        LLInventoryObject::object_list_t::const_iterator it = mInventory->begin();
        LLInventoryObject::object_list_t::const_iterator end = mInventory->end();
        for(  ; it != end ; ++it )
        {
            if( (*it)->getType() == type )
            {
                ++count;
            }
        }
    }
    return count;
}

void LLViewerObject::setDebugText(const std::string &utf8text, const LLColor4& color)
{
    if (utf8text.empty() && !mText)
    {
        return;
    }

    if (!mText)
    {
        initHudText();
    }
    mText->setColor(color);
    mText->setString(utf8text);
    mText->setZCompare(false);
    mText->setDoFade(false);
    updateText();
}

void LLViewerObject::appendDebugText(const std::string &utf8text)
{
    if (utf8text.empty() && !mText)
    {
        return;
    }

    if (!mText)
    {
        initHudText();
    }
    mText->addLine(utf8text, LLColor4::white);
    mText->setZCompare(false);
    mText->setDoFade(false);
    updateText();
}

void LLViewerObject::initHudText()
{
    mText = (LLHUDText *)LLHUDObject::addHUDObject(LLHUDObject::LL_HUD_TEXT);
    mText->setFont(LLFontGL::getFontSansSerif());
    mText->setVertAlignment(LLHUDText::ALIGN_VERT_TOP);
    mText->setMaxLines(-1);
    mText->setSourceObject(this);
    mText->setOnHUDAttachment(isHUDAttachment());
}

void LLViewerObject::restoreHudText()
{
    if (mHudText.empty())
    {
        if (mText)
        {
            mText->markDead();
            mText = NULL;
        }
    }
    else
    {
        if (!mText)
        {
            initHudText();
        }
        else
        {
            // Restore default values
            mText->setZCompare(true);
            mText->setDoFade(true);
        }
        mText->setColor(mHudTextColor);
        mText->setString(mHudText);
    }
}

void LLViewerObject::setIcon(LLViewerTexture* icon_image)
{
    if (!mIcon)
    {
        mIcon = (LLHUDIcon *)LLHUDObject::addHUDObject(LLHUDObject::LL_HUD_ICON);
        mIcon->setSourceObject(this);
        mIcon->setImage(icon_image);
        // *TODO: make this user configurable
        mIcon->setScale(0.03f);
    }
    else
    {
        mIcon->restartLifeTimer();
    }
}

void LLViewerObject::clearIcon()
{
    if (mIcon)
    {
        mIcon = NULL;
    }
}

LLViewerObject* LLViewerObject::getSubParent()
{
    return (LLViewerObject*) getParent();
}

const LLViewerObject* LLViewerObject::getSubParent() const
{
    return (const LLViewerObject*) getParent();
}

bool LLViewerObject::isOnMap()
{
    return mOnMap;
}


void LLViewerObject::updateText()
{
    if (!isDead())
    {
        if (mText.notNull())
        {
            LLVOAvatar* avatar = getAvatar();
            if (avatar)
            {
                mText->setHidden(avatar->isInMuteList());
            }

            LLVector3 up_offset(0,0,0);
            up_offset.mV[2] = getScale().mV[VZ]*0.6f;

            if (mDrawable.notNull())
            {
                mText->setPositionAgent(getRenderPosition() + up_offset);
            }
            else
            {
                mText->setPositionAgent(getPositionAgent() + up_offset);
            }
        }
    }
}

bool LLViewerObject::isOwnerInMuteList(LLUUID id)
{
    LLUUID owner_id = id.isNull() ? mOwnerID : id;
    if (isAvatar() || owner_id.isNull())
    {
        return false;
    }
    bool muted = false;
    F64 now = LLFrameTimer::getTotalSeconds();
    if (now < mCachedMuteListUpdateTime)
    {
        muted = mCachedOwnerInMuteList;
    }
    else
    {
        muted = LLMuteList::getInstance()->isMuted(owner_id);

        const F64 SECONDS_BETWEEN_MUTE_UPDATES = 1;
        mCachedMuteListUpdateTime = now + SECONDS_BETWEEN_MUTE_UPDATES;
        mCachedOwnerInMuteList = muted;
    }
    return muted;
}

LLVOAvatar* LLViewerObject::asAvatar()
{
    return NULL;
}

// If this object is directly or indirectly parented by an avatar,
// return it.  Normally getAvatar() is the correct function to call;
// it will give the avatar used for skinning.  The exception is with
// animated objects that are also attachments; in that case,
// getAvatar() will return the control avatar, used for skinning, and
// getAvatarAncestor will return the avatar to which the object is
// attached.
LLVOAvatar* LLViewerObject::getAvatarAncestor()
{
    LLViewerObject *pobj = (LLViewerObject*) getParent();
    while (pobj)
    {
        LLVOAvatar *av = pobj->asAvatar();
        if (av)
        {
            return av;
        }
        pobj =  (LLViewerObject*) pobj->getParent();
    }
    return NULL;
}

bool LLViewerObject::isParticleSource() const
{
    return !mPartSourcep.isNull() && !mPartSourcep->isDead();
}

void LLViewerObject::setParticleSource(const LLPartSysData& particle_parameters, const LLUUID& owner_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VIEWER;
    if (mPartSourcep)
    {
        deleteParticleSource();
    }

    LLPointer<LLViewerPartSourceScript> pss = LLViewerPartSourceScript::createPSS(this, particle_parameters);
    mPartSourcep = pss;

    if (mPartSourcep)
    {
        mPartSourcep->setOwnerUUID(owner_id);

        if (mPartSourcep->getImage()->getID() != mPartSourcep->mPartSysData.mPartImageID)
        {
            LLViewerTexture* image;
            if (mPartSourcep->mPartSysData.mPartImageID == LLUUID::null)
            {
                image = LLViewerTextureManager::getFetchedTextureFromFile("pixiesmall.tga");
            }
            else
            {
                image = LLViewerTextureManager::getFetchedTexture(mPartSourcep->mPartSysData.mPartImageID);
            }
            mPartSourcep->setImage(image);
        }
    }
    LLViewerPartSim::getInstance()->addPartSource(pss);
}

void LLViewerObject::unpackParticleSource(const S32 block_num, const LLUUID& owner_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VIEWER;
    if (!mPartSourcep.isNull() && mPartSourcep->isDead())
    {
        mPartSourcep = NULL;
    }
    if (mPartSourcep)
    {
        // If we've got one already, just update the existing source (or remove it)
        if (!LLViewerPartSourceScript::unpackPSS(this, mPartSourcep, block_num))
        {
            mPartSourcep->setDead();
            mPartSourcep = NULL;
        }
    }
    else
    {
        LLPointer<LLViewerPartSourceScript> pss = LLViewerPartSourceScript::unpackPSS(this, NULL, block_num);
        //If the owner is muted, don't create the system
        if(LLMuteList::getInstance()->isMuted(owner_id, LLMute::flagParticles)) return;

        // We need to be able to deal with a particle source that hasn't changed, but still got an update!
        if (pss)
        {
//          LL_INFOS() << "Making particle system with owner " << owner_id << LL_ENDL;
            pss->setOwnerUUID(owner_id);
            mPartSourcep = pss;
            LLViewerPartSim::getInstance()->addPartSource(pss);
        }
    }
    if (mPartSourcep)
    {
        if (mPartSourcep->getImage()->getID() != mPartSourcep->mPartSysData.mPartImageID)
        {
            LLViewerTexture* image;
            if (mPartSourcep->mPartSysData.mPartImageID == LLUUID::null)
            {
                image = LLViewerFetchedTexture::sDefaultParticleImagep;
            }
            else
            {
                image = LLViewerTextureManager::getFetchedTexture(mPartSourcep->mPartSysData.mPartImageID);
            }
            mPartSourcep->setImage(image);
        }
    }
}

void LLViewerObject::unpackParticleSource(LLDataPacker &dp, const LLUUID& owner_id, bool legacy)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VIEWER;
    if (!mPartSourcep.isNull() && mPartSourcep->isDead())
    {
        mPartSourcep = NULL;
    }
    if (mPartSourcep)
    {
        // If we've got one already, just update the existing source (or remove it)
        if (!LLViewerPartSourceScript::unpackPSS(this, mPartSourcep, dp, legacy))
        {
            mPartSourcep->setDead();
            mPartSourcep = NULL;
        }
    }
    else
    {
        LLPointer<LLViewerPartSourceScript> pss = LLViewerPartSourceScript::unpackPSS(this, NULL, dp, legacy);
        //If the owner is muted, don't create the system
        if(LLMuteList::getInstance()->isMuted(owner_id, LLMute::flagParticles)) return;
        // We need to be able to deal with a particle source that hasn't changed, but still got an update!
        if (pss)
        {
//          LL_INFOS() << "Making particle system with owner " << owner_id << LL_ENDL;
            pss->setOwnerUUID(owner_id);
            mPartSourcep = pss;
            LLViewerPartSim::getInstance()->addPartSource(pss);
        }
    }
    if (mPartSourcep)
    {
        if (mPartSourcep->getImage()->getID() != mPartSourcep->mPartSysData.mPartImageID)
        {
            LLViewerTexture* image;
            if (mPartSourcep->mPartSysData.mPartImageID == LLUUID::null)
            {
                image = LLViewerFetchedTexture::sDefaultParticleImagep;
            }
            else
            {
                image = LLViewerTextureManager::getFetchedTexture(mPartSourcep->mPartSysData.mPartImageID);
            }
            mPartSourcep->setImage(image);
        }
    }
}

void LLViewerObject::deleteParticleSource()
{
    if (mPartSourcep.notNull())
    {
        mPartSourcep->setDead();
        mPartSourcep = NULL;
    }
}

// virtual
void LLViewerObject::updateDrawable(bool force_damped)
{
    if (!isChanged(MOVED))
    { //most common case, having an empty if case here makes for better branch prediction
    }
    else if (mDrawable.notNull() &&
        !mDrawable->isState(LLDrawable::ON_MOVE_LIST))
    {
        bool damped_motion =
            !isChanged(SHIFTED) &&                                      // not shifted between regions this frame and...
            (   force_damped ||                                     // ...forced into damped motion by application logic or...
                (   !isSelected() &&                                    // ...not selected and...
                    (   mDrawable->isRoot() ||                              // ... is root or ...
                        (getParent() && !((LLViewerObject*)getParent())->isSelected())// ... parent is not selected and ...
                    ) &&
                    getPCode() == LL_PCODE_VOLUME &&                    // ...is a volume object and...
                    getVelocity().isExactlyZero() &&                    // ...is not moving physically and...
                    mDrawable->getGeneration() != -1                    // ...was not created this frame.
                )
            );
        gPipeline.markMoved(mDrawable, damped_motion);
    }
    clearChanged(SHIFTED);
}

// virtual, overridden by LLVOVolume
F32 LLViewerObject::getVObjRadius() const
{
    return mDrawable.notNull() ? mDrawable->getRadius() : 0.f;
}

void LLViewerObject::setAttachedSound(const LLUUID &audio_uuid, const LLUUID& owner_id, const F32 gain, const U8 flags)
{
    if (!gAudiop)
    {
        return;
    }

    if (audio_uuid.isNull())
    {
        if (!mAudioSourcep)
        {
            return;
        }
        if (mAudioSourcep->isLoop() && !mAudioSourcep->hasPendingPreloads())
        {
            // We don't clear the sound if it's a loop, it'll go away on its own.
            // At least, this appears to be how the scripts work.
            // The attached sound ID is set to NULL to avoid it playing back when the
            // object rezzes in on non-looping sounds.
            //LL_INFOS() << "Clearing attached sound " << mAudioSourcep->getCurrentData()->getID() << LL_ENDL;
            gAudiop->cleanupAudioSource(mAudioSourcep);
            mAudioSourcep = NULL;
        }
        else if (flags & LL_SOUND_FLAG_STOP)
        {
            // Just shut off the sound
            mAudioSourcep->stop();
        }
        return;
    }
    if (flags & LL_SOUND_FLAG_LOOP
        && mAudioSourcep && mAudioSourcep->isLoop() && mAudioSourcep->getCurrentData()
        && mAudioSourcep->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already playing this sound on a loop, ignoring" << LL_ENDL;
        return;
    }

    // don't clean up before previous sound is done. Solves: SL-33486
    if ( mAudioSourcep && mAudioSourcep->isDone() )
    {
        gAudiop->cleanupAudioSource(mAudioSourcep);
        mAudioSourcep = NULL;
    }

    if (mAudioSourcep && mAudioSourcep->isMuted() &&
        mAudioSourcep->getCurrentData() && mAudioSourcep->getCurrentData()->getID() == audio_uuid)
    {
        //LL_INFOS() << "Already having this sound as muted sound, ignoring" << LL_ENDL;
        return;
    }

    getAudioSource(owner_id);

    if (mAudioSourcep)
    {
        bool queue = flags & LL_SOUND_FLAG_QUEUE;
        mAudioGain = gain;
        mAudioSourcep->setGain(gain);
        mAudioSourcep->setLoop(flags & LL_SOUND_FLAG_LOOP);
        mAudioSourcep->setSyncMaster(flags & LL_SOUND_FLAG_SYNC_MASTER);
        mAudioSourcep->setSyncSlave(flags & LL_SOUND_FLAG_SYNC_SLAVE);
        mAudioSourcep->setQueueSounds(queue);
        if(!queue) // stop any current sound first to avoid "farts of doom" (SL-1541) -MG
        {
            mAudioSourcep->stop();
        }

        // Play this sound if region maturity permits
        if( gAgent.canAccessMaturityAtGlobal(this->getPositionGlobal()) )
        {
            //LL_INFOS() << "Playing attached sound " << audio_uuid << LL_ENDL;
            // recheck cutoff radius in case this update was an object-update with new value
            mAudioSourcep->checkCutOffRadius();
            mAudioSourcep->play(audio_uuid);
        }
    }
}

LLAudioSource *LLViewerObject::getAudioSource(const LLUUID& owner_id)
{
    if (!mAudioSourcep)
    {
        // Arbitrary low gain for a sound that's not playing.
        // This is used for sound preloads, for example.
        LLAudioSourceVO *asvop = new LLAudioSourceVO(mID, owner_id, 0.01f, this);

        mAudioSourcep = asvop;
        if(gAudiop)
        {
            gAudiop->addAudioSource(asvop);
        }
    }

    return mAudioSourcep;
}

void LLViewerObject::adjustAudioGain(const F32 gain)
{
    if (mAudioSourcep)
    {
        mAudioGain = gain;
        mAudioSourcep->setGain(mAudioGain);
    }
}

//----------------------------------------------------------------------------

bool LLViewerObject::unpackParameterEntry(U16 param_type, LLDataPacker *dp)
{
    if (LLNetworkData::PARAMS_MESH == param_type)
    {
        param_type = LLNetworkData::PARAMS_SCULPT;
    }
    ExtraParameter* param = getExtraParameterEntryCreate(param_type);
    if (param)
    {
        param->data->unpack(*dp);
        param->in_use = true;
        parameterChanged(param_type, param->data, true, false);
        return true;
    }
    else
    {
        return false;
    }
}

LLViewerObject::ExtraParameter* LLViewerObject::createNewParameterEntry(U16 param_type)
{
    LLNetworkData* new_block = NULL;
    switch (param_type)
    {
      case LLNetworkData::PARAMS_FLEXIBLE:
      {
          new_block = new LLFlexibleObjectData();
          break;
      }
      case LLNetworkData::PARAMS_LIGHT:
      {
          new_block = new LLLightParams();
          break;
      }
      case LLNetworkData::PARAMS_SCULPT:
      {
          new_block = new LLSculptParams();
          break;
      }
      case LLNetworkData::PARAMS_LIGHT_IMAGE:
      {
          new_block = new LLLightImageParams();
          break;
      }
      case LLNetworkData::PARAMS_EXTENDED_MESH:
      {
          new_block = new LLExtendedMeshParams();
          break;
      }
      case LLNetworkData::PARAMS_RENDER_MATERIAL:
      {
          new_block = new LLRenderMaterialParams();
          break;
      }
      case LLNetworkData::PARAMS_REFLECTION_PROBE:
      {
          new_block = new LLReflectionProbeParams();
          break;
      }
      default:
      {
          LL_INFOS_ONCE() << "Unknown param type: " << param_type << LL_ENDL;
          break;
      }
    };

    if (new_block)
    {
        ExtraParameter* new_entry = new ExtraParameter;
        new_entry->data = new_block;
        new_entry->in_use = false; // not in use yet
        llassert(mExtraParameterList[param_type] == nullptr); // leak -- redundantly allocated parameter entry
        mExtraParameterList[param_type] = new_entry;
        return new_entry;
    }
    return NULL;
}

LLViewerObject::ExtraParameter* LLViewerObject::getExtraParameterEntry(U16 param_type) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VIEWER;
    std::unordered_map<U16, ExtraParameter*>::const_iterator itor = mExtraParameterList.find(param_type);
    if (itor != mExtraParameterList.end())
    {
        return itor->second;
    }
    return NULL;
}

LLViewerObject::ExtraParameter* LLViewerObject::getExtraParameterEntryCreate(U16 param_type)
{
    ExtraParameter* param = getExtraParameterEntry(param_type);
    if (!param)
    {
        param = createNewParameterEntry(param_type);
    }
    return param;
}

LLNetworkData* LLViewerObject::getParameterEntry(U16 param_type) const
{
    ExtraParameter* param = getExtraParameterEntry(param_type);
    if (param)
    {
        return param->data;
    }
    else
    {
        return NULL;
    }
}

bool LLViewerObject::getParameterEntryInUse(U16 param_type) const
{
    ExtraParameter* param = getExtraParameterEntry(param_type);
    if (param)
    {
        return param->in_use;
    }
    else
    {
        return false;
    }
}

bool LLViewerObject::setParameterEntry(U16 param_type, const LLNetworkData& new_value, bool local_origin)
{
    ExtraParameter* param = getExtraParameterEntryCreate(param_type);
    if (param)
    {
        if (param->in_use && new_value == *(param->data))
        {
            return false;
        }
        param->in_use = true;
        param->data->copy(new_value);
        parameterChanged(param_type, param->data, true, local_origin);
        return true;
    }
    else
    {
        return false;
    }
}

// Assumed to be called locally
// If in_use is true, will crate a new extra parameter if none exists.
// Should always return true.
bool LLViewerObject::setParameterEntryInUse(U16 param_type, bool in_use, bool local_origin)
{
    ExtraParameter* param = getExtraParameterEntryCreate(param_type);
    if (param && param->in_use != in_use)
    {
        param->in_use = in_use;
        parameterChanged(param_type, param->data, in_use, local_origin);
        return true;
    }
    return false;
}

void LLViewerObject::parameterChanged(U16 param_type, bool local_origin)
{
    ExtraParameter* param = getExtraParameterEntry(param_type);
    if (param)
    {
        parameterChanged(param_type, param->data, param->in_use, local_origin);
    }
}

void LLViewerObject::parameterChanged(U16 param_type, LLNetworkData* data, bool in_use, bool local_origin)
{
    if (local_origin)
    {
        // *NOTE: Do not send the render material ID in this way as it will get
        // out-of-sync with other sent client data.
        // See LLViewerObject::setRenderMaterialID and LLGLTFMaterialList
        llassert(param_type != LLNetworkData::PARAMS_RENDER_MATERIAL);

        LLViewerRegion* regionp = getRegion();
        if(!regionp) return;

        // Change happened on the viewer. Send the change up
        U8 tmp[MAX_OBJECT_PARAMS_SIZE];
        LLDataPackerBinaryBuffer dpb(tmp, MAX_OBJECT_PARAMS_SIZE);
        if (data->pack(dpb))
        {
            U32 datasize = (U32)dpb.getCurrentSize();

            LLMessageSystem* msg = gMessageSystem;
            msg->newMessageFast(_PREHASH_ObjectExtraParams);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
            msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
            msg->nextBlockFast(_PREHASH_ObjectData);
            msg->addU32Fast(_PREHASH_ObjectLocalID, mLocalID );

            msg->addU16Fast(_PREHASH_ParamType, param_type);
            msg->addBOOLFast(_PREHASH_ParamInUse, in_use);

            msg->addU32Fast(_PREHASH_ParamSize, datasize);
            msg->addBinaryDataFast(_PREHASH_ParamData, tmp, datasize);

            msg->sendReliable( regionp->getHost() );
        }
        else
        {
            LL_WARNS() << "Failed to send object extra parameters: " << param_type << LL_ENDL;
        }
    }
    else
    {
        if (param_type == LLNetworkData::PARAMS_RENDER_MATERIAL)
        {
            const LLRenderMaterialParams* params = in_use ? (LLRenderMaterialParams*)getParameterEntry(LLNetworkData::PARAMS_RENDER_MATERIAL) : nullptr;
            setRenderMaterialIDs(params, local_origin);
        }
    }
}

void LLViewerObject::setDrawableState(U32 state, bool recursive)
{
    if (mDrawable)
    {
        mDrawable->setState(state);
    }
    if (recursive)
    {
        for (child_list_t::iterator iter = mChildList.begin();
             iter != mChildList.end(); iter++)
        {
            LLViewerObject* child = *iter;
            child->setDrawableState(state, recursive);
        }
    }
}

void LLViewerObject::clearDrawableState(U32 state, bool recursive)
{
    if (mDrawable)
    {
        mDrawable->clearState(state);
    }
    if (recursive)
    {
        for (child_list_t::iterator iter = mChildList.begin();
             iter != mChildList.end(); iter++)
        {
            LLViewerObject* child = *iter;
            child->clearDrawableState(state, recursive);
        }
    }
}

bool LLViewerObject::isDrawableState(U32 state, bool recursive) const
{
    bool matches = false;
    if (mDrawable)
    {
        matches = mDrawable->isState(state);
    }
    if (recursive)
    {
        for (child_list_t::const_iterator iter = mChildList.begin();
             (iter != mChildList.end()) && matches; iter++)
        {
            LLViewerObject* child = *iter;
            matches &= child->isDrawableState(state, recursive);
        }
    }

    return matches;
}



//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// RN: these functions assume a 2-level hierarchy
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

// Owned by anyone?
bool LLViewerObject::permAnyOwner() const
{
    if (isRootEdit())
    {
        return flagObjectAnyOwner();
    }
    else
    {
        return ((LLViewerObject*)getParent())->permAnyOwner();
    }
}
// Owned by this viewer?
bool LLViewerObject::permYouOwner() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
        {
            return true;
        }
# endif
        return flagObjectYouOwner();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permYouOwner();
    }
}

// Owned by a group?
bool LLViewerObject::permGroupOwner() const
{
    if (isRootEdit())
    {
        return flagObjectGroupOwned();
    }
    else
    {
        return ((LLViewerObject*)getParent())->permGroupOwner();
    }
}

// Can the owner edit
bool LLViewerObject::permOwnerModify() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
    {
            return true;
    }
# endif
        return flagObjectOwnerModify();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permOwnerModify();
    }
}

// Can edit
bool LLViewerObject::permModify() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
    {
            return true;
    }
# endif
        return flagObjectModify();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permModify();
    }
}

// Can copy
bool LLViewerObject::permCopy() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
        {
            return true;
        }
# endif
        return flagObjectCopy();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permCopy();
    }
}

// Can move
bool LLViewerObject::permMove() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
        {
            return true;
        }
# endif
        return flagObjectMove();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permMove();
    }
}

// Can be transferred
bool LLViewerObject::permTransfer() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (!LLGridManager::getInstance()->isInProductionGrid()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
        {
            return true;
        }
# endif
        return flagObjectTransfer();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permTransfer();
    }
}

// Can only open objects that you own, or that someone has
// given you modify rights to.  JC
bool LLViewerObject::allowOpen() const
{
    return !flagInventoryEmpty() && (permYouOwner() || permModify());
}

LLViewerObject::LLInventoryCallbackInfo::~LLInventoryCallbackInfo()
{
    if (mListener)
    {
        mListener->clearVOInventoryListener();
    }
}

void LLViewerObject::updateVolume(const LLVolumeParams& volume_params)
{
    if (setVolume(volume_params, 1)) // *FIX: magic number, ack!
    {
        // Transmit the update to the simulator
        sendShapeUpdate();
        markForUpdate();
    }
}

void LLViewerObject::recursiveMarkForUpdate()
{
    for (LLViewerObject::child_list_t::iterator iter = mChildList.begin();
         iter != mChildList.end(); iter++)
    {
        LLViewerObject* child = *iter;
        child->markForUpdate();
    }
    markForUpdate();
}

void LLViewerObject::markForUpdate()
{
    if (mDrawable.notNull())
    {
        gPipeline.markTextured(mDrawable);
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_GEOMETRY);
    }
}

bool LLViewerObject::isPermanentEnforced() const
{
    return flagObjectPermanent() && (mRegionp != gAgent.getRegion()) && !gAgent.isGodlike();
}

bool LLViewerObject::getIncludeInSearch() const
{
    return flagIncludeInSearch();
}

void LLViewerObject::setIncludeInSearch(bool include_in_search)
{
    setFlags(FLAGS_INCLUDE_IN_SEARCH, include_in_search);
}

void LLViewerObject::setRegion(LLViewerRegion *regionp)
{
    if (!regionp)
    {
        LL_WARNS() << "viewer object set region to NULL" << LL_ENDL;
    }
    if(regionp != mRegionp)
    {
        if(mRegionp)
        {
            mRegionp->removeFromCreatedList(getLocalID());
        }
        if(regionp)
        {
            regionp->addToCreatedList(getLocalID());
        }
    }

    mLatestRecvPacketID = 0;
    mRegionp = regionp;

    for (child_list_t::iterator i = mChildList.begin(); i != mChildList.end(); ++i)
    {
        LLViewerObject* child = *i;
        child->setRegion(regionp);
    }

    if (mControlAvatar)
    {
        mControlAvatar->setRegion(regionp);
    }

    setChanged(MOVED | SILHOUETTE);
    updateDrawable(false);
}

// virtual
void    LLViewerObject::updateRegion(LLViewerRegion *regionp)
{
//  if (regionp)
//  {
//      F64 now = LLFrameTimer::getElapsedSeconds();
//      LL_INFOS() << "Updating to region " << regionp->getName()
//          << ", ms since last update message: " << (F32)((now - mLastMessageUpdateSecs) * 1000.0)
//          << ", ms since last interpolation: " << (F32)((now - mLastInterpUpdateSecs) * 1000.0)
//          << LL_ENDL;
//  }
}


bool LLViewerObject::specialHoverCursor() const
{
    return flagUsePhysics()
            || flagHandleTouch()
            || (mClickAction != 0);
}

void LLViewerObject::updateFlags(bool physics_changed)
{
    LLViewerRegion* regionp = getRegion();
    if(!regionp) return;
    gMessageSystem->newMessage("ObjectFlagUpdate");
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, getLocalID() );
    gMessageSystem->addBOOLFast(_PREHASH_UsePhysics, flagUsePhysics() );
    gMessageSystem->addBOOL("IsTemporary", flagTemporaryOnRez() );
    gMessageSystem->addBOOL("IsPhantom", flagPhantom() );

    // stinson 02/28/2012 : This CastsShadows bool is no longer used in either the viewer or the simulator
    // The simulator code does not even unpack this value when the message is received.
    // This could be potentially hijacked in the future for another use should the urgent need arise.
    gMessageSystem->addBOOL("CastsShadows", false );

    if (physics_changed)
    {
        gMessageSystem->nextBlock("ExtraPhysics");
        gMessageSystem->addU8("PhysicsShapeType", getPhysicsShapeType() );
        gMessageSystem->addF32("Density", getPhysicsDensity() );
        gMessageSystem->addF32("Friction", getPhysicsFriction() );
        gMessageSystem->addF32("Restitution", getPhysicsRestitution() );
        gMessageSystem->addF32("GravityMultiplier", getPhysicsGravity() );
    }
    gMessageSystem->sendReliable( regionp->getHost() );
}

bool LLViewerObject::setFlags(U32 flags, bool state)
{
    bool setit = setFlagsWithoutUpdate(flags, state);

    // BUG: Sometimes viewer physics and simulator physics get
    // out of sync.  To fix this, always send update to simulator.
//  if (setit)
    {
        updateFlags();
    }
    return setit;
}

bool LLViewerObject::setFlagsWithoutUpdate(U32 flags, bool state)
{
    bool setit = false;
    if (state)
    {
        if ((mFlags & flags) != flags)
        {
            mFlags |= flags;
            setit = true;
        }
    }
    else
    {
        if ((mFlags & flags) != 0)
        {
            mFlags &= ~flags;
            setit = true;
        }
    }
    return setit;
}

void LLViewerObject::setPhysicsShapeType(U8 type)
{
    mPhysicsShapeUnknown = false;
    if (type != mPhysicsShapeType)
    {
    mPhysicsShapeType = type;
    setObjectCostStale();
}
}

void LLViewerObject::setPhysicsGravity(F32 gravity)
{
    mPhysicsGravity = gravity;
}

void LLViewerObject::setPhysicsFriction(F32 friction)
{
    mPhysicsFriction = friction;
}

void LLViewerObject::setPhysicsDensity(F32 density)
{
    mPhysicsDensity = density;
}

void LLViewerObject::setPhysicsRestitution(F32 restitution)
{
    mPhysicsRestitution = restitution;
}

U8 LLViewerObject::getPhysicsShapeType() const
{
    if (mPhysicsShapeUnknown)
    {
        gObjectList.updatePhysicsFlags(this);
    }

    return mPhysicsShapeType;
}

void LLViewerObject::applyAngularVelocity(F32 dt)
{
    //do target omega here
    mRotTime += dt;
    LLVector3 ang_vel = getAngularVelocity();
    F32 omega = ang_vel.magVecSquared();
    F32 angle = 0.0f;
    LLQuaternion dQ;
    if (omega > 0.00001f)
    {
        omega = sqrt(omega);
        angle = omega * dt;

        ang_vel *= 1.f/omega;

        // calculate the delta increment based on the object's angular velocity
        dQ.setQuat(angle, ang_vel);

        // accumulate the angular velocity rotations to re-apply in the case of an object update
        mAngularVelocityRot *= dQ;

        // Just apply the delta increment to the current rotation
        setRotation(getRotation()*dQ);
        setChanged(MOVED | SILHOUETTE);
    }
}

void LLViewerObject::resetRotTime()
{
    mRotTime = 0.0f;
}

void LLViewerObject::resetRot()
{
    resetRotTime();

    // Reset the accumulated angular velocity rotation
    mAngularVelocityRot.loadIdentity();
}

U32 LLViewerObject::getPartitionType() const
{
    return LLViewerRegion::PARTITION_NONE;
}

void LLViewerObject::dirtySpatialGroup() const
{
    if (mDrawable)
    {
        LLSpatialGroup* group = mDrawable->getSpatialGroup();
        if (group)
        {
            group->dirtyGeom();
            gPipeline.markRebuild(group);
        }
    }
}

void LLViewerObject::dirtyMesh()
{
    if (mDrawable)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
    }
}

F32 LLAlphaObject::getPartSize(S32 idx)
{
    return 0.f;
}

void LLAlphaObject::getBlendFunc(S32 face, LLRender::eBlendFactor& src, LLRender::eBlendFactor& dst)
{

}

// virtual
void LLStaticViewerObject::updateDrawable(bool force_damped)
{
    // Force an immediate rebuild on any update
    if (mDrawable.notNull())
    {
        mDrawable->updateXform(true);
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
    }
    clearChanged(SHIFTED);
}

void LLViewerObject::saveUnselectedChildrenPosition(std::vector<LLVector3>& positions)
{
    if(mChildList.empty() || !positions.empty())
    {
        return ;
    }

    for (LLViewerObject::child_list_t::const_iterator iter = mChildList.begin();
            iter != mChildList.end(); iter++)
    {
        LLViewerObject* childp = *iter;
        if (!childp->isSelected() && childp->mDrawable.notNull())
        {
            positions.push_back(childp->getPositionEdit());
        }
    }

    return ;
}

void LLViewerObject::saveUnselectedChildrenRotation(std::vector<LLQuaternion>& rotations)
{
    if(mChildList.empty())
    {
        return ;
    }

    for (LLViewerObject::child_list_t::const_iterator iter = mChildList.begin();
            iter != mChildList.end(); iter++)
    {
        LLViewerObject* childp = *iter;
        if (!childp->isSelected() && childp->mDrawable.notNull())
        {
            rotations.push_back(childp->getRotationEdit());
        }
    }

    return ;
}

//counter-rotation
void LLViewerObject::resetChildrenRotationAndPosition(const std::vector<LLQuaternion>& rotations,
                                            const std::vector<LLVector3>& positions)
{
    if(mChildList.empty())
    {
        return ;
    }

    S32 index = 0 ;
    LLQuaternion inv_rotation = ~getRotationEdit() ;
    LLVector3 offset = getPositionEdit() ;
    for (LLViewerObject::child_list_t::const_iterator iter = mChildList.begin();
            iter != mChildList.end(); iter++)
    {
        LLViewerObject* childp = *iter;
        if (!childp->isSelected() && childp->mDrawable.notNull())
        {
            if (childp->getPCode() != LL_PCODE_LEGACY_AVATAR)
            {
                childp->setRotation(rotations[index] * inv_rotation);
                childp->setPosition((positions[index] - offset) * inv_rotation);
                LLManip::rebuild(childp);
            }
            else //avatar
            {
                LLVector3 reset_pos = (positions[index] - offset) * inv_rotation ;
                LLQuaternion reset_rot = rotations[index] * inv_rotation ;

                ((LLVOAvatar*)childp)->mDrawable->mXform.setPosition(reset_pos);
                ((LLVOAvatar*)childp)->mDrawable->mXform.setRotation(reset_rot) ;

                ((LLVOAvatar*)childp)->mDrawable->getVObj()->setPosition(reset_pos, true);
                ((LLVOAvatar*)childp)->mDrawable->getVObj()->setRotation(reset_rot, true) ;

                LLManip::rebuild(childp);
            }
            index++;
        }
    }

    return ;
}

//counter-translation
void LLViewerObject::resetChildrenPosition(const LLVector3& offset, bool simplified, bool skip_avatar_child)
{
    if(mChildList.empty())
    {
        return ;
    }

    LLVector3 child_offset;
    if(simplified) //translation only, rotation matrix does not change
    {
        child_offset = offset * ~getRotation();
    }
    else //rotation matrix might change too.
    {
        if (isAttachment() && mDrawable.notNull())
        {
            LLXform* attachment_point_xform = mDrawable->getXform()->getParent();
            LLQuaternion parent_rotation = getRotation() * attachment_point_xform->getWorldRotation();
            child_offset = offset * ~parent_rotation;
        }
        else
        {
            child_offset = offset * ~getRenderRotation();
        }
    }

    for (LLViewerObject::child_list_t::const_iterator iter = mChildList.begin();
            iter != mChildList.end(); iter++)
    {
        LLViewerObject* childp = *iter;

        if (!childp->isSelected() && childp->mDrawable.notNull())
        {
            if (childp->getPCode() != LL_PCODE_LEGACY_AVATAR)
            {
                childp->setPosition(childp->getPosition() + child_offset);
                LLManip::rebuild(childp);
            }
            else //avatar
            {
                if(!skip_avatar_child)
                {
                    LLVector3 reset_pos = ((LLVOAvatar*)childp)->mDrawable->mXform.getPosition() + child_offset ;

                    ((LLVOAvatar*)childp)->mDrawable->mXform.setPosition(reset_pos);
                    ((LLVOAvatar*)childp)->mDrawable->getVObj()->setPosition(reset_pos);
                    LLManip::rebuild(childp);
                }
            }
        }
    }

    return ;
}

// virtual
bool    LLViewerObject::isTempAttachment() const
{
    return (mID.notNull() && (mID == mAttachmentItemID));
}

bool LLViewerObject::isHiglightedOrBeacon() const
{
    if (LLFloaterReg::instanceVisible("beacons") && (gPipeline.getRenderBeacons() || gPipeline.getRenderHighlights()))
    {
        bool has_media = (getMediaType() == LLViewerObject::MEDIA_SET);
        bool is_scripted = !isAvatar() && !getParent() && flagScripted();
        bool is_physical = !isAvatar() && flagUsePhysics();

        return (isParticleSource() && gPipeline.getRenderParticleBeacons())
                || (isAudioSource() && gPipeline.getRenderSoundBeacons())
                || (has_media && gPipeline.getRenderMOAPBeacons())
                || (is_scripted && gPipeline.getRenderScriptedBeacons())
                || (is_scripted && flagHandleTouch() && gPipeline.getRenderScriptedTouchBeacons())
                || (is_physical && gPipeline.getRenderPhysicalBeacons());
    }
    return false;
}


const LLUUID &LLViewerObject::getAttachmentItemID() const
{
    return mAttachmentItemID;
}

void LLViewerObject::setAttachmentItemID(const LLUUID &id)
{
    mAttachmentItemID = id;
}

EObjectUpdateType LLViewerObject::getLastUpdateType() const
{
    return mLastUpdateType;
}

void LLViewerObject::setLastUpdateType(EObjectUpdateType last_update_type)
{
    mLastUpdateType = last_update_type;
}

bool LLViewerObject::getLastUpdateCached() const
{
    return mLastUpdateCached;
}

void LLViewerObject::setLastUpdateCached(bool last_update_cached)
{
    mLastUpdateCached = last_update_cached;
}

const LLUUID &LLViewerObject::extractAttachmentItemID()
{
    LLUUID item_id = LLUUID::null;
    LLNameValue* item_id_nv = getNVPair("AttachItemID");
    if( item_id_nv )
    {
        const char* s = item_id_nv->getString();
        if( s )
        {
            item_id.set(s);
        }
    }
    setAttachmentItemID(item_id);
    return getAttachmentItemID();
}

const std::string& LLViewerObject::getAttachmentItemName() const
{
    static std::string empty;
    LLInventoryItem *item = gInventory.getItem(getAttachmentItemID());
    if (isAttachment() && item)
    {
        return item->getName();
    }
    return empty;
}

//virtual
LLVOAvatar* LLViewerObject::getAvatar() const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    if (getControlAvatar())
    {
        return getControlAvatar();
    }
    if (isAttachment())
    {
        LLViewerObject* vobj = (LLViewerObject*) getParent();

        while (vobj && !vobj->asAvatar())
        {
            vobj = (LLViewerObject*) vobj->getParent();
        }

        return (LLVOAvatar*) vobj;
    }

    return NULL;
}

bool LLViewerObject::hasRenderMaterialParams() const
{
    return getParameterEntryInUse(LLNetworkData::PARAMS_RENDER_MATERIAL);
}

void LLViewerObject::setHasRenderMaterialParams(bool has_materials)
{
    bool had_materials = hasRenderMaterialParams();

    if (had_materials != has_materials)
    {
        if (has_materials)
        {
            setParameterEntryInUse(LLNetworkData::PARAMS_RENDER_MATERIAL, true, true);
        }
        else
        {
            setParameterEntryInUse(LLNetworkData::PARAMS_RENDER_MATERIAL, false, true);
        }
    }
}

const LLUUID& LLViewerObject::getRenderMaterialID(U8 te) const
{
    LLRenderMaterialParams* param_block = (LLRenderMaterialParams*)getParameterEntry(LLNetworkData::PARAMS_RENDER_MATERIAL);
    if (param_block)
    {
        return param_block->getMaterial(te);
    }

    return LLUUID::null;
}

void LLViewerObject::rebuildMaterial()
{
    llassert(!isDead());

    faceMappingChanged();
    gPipeline.markTextured(mDrawable);
}

void LLViewerObject::setRenderMaterialID(S32 te_in, const LLUUID& id, bool update_server, bool local_origin)
{
    // implementation is delicate

    // if update is bound for server, should always null out GLTFRenderMaterial and clear GLTFMaterialOverride even if ids haven't changed
    //  (the case where ids haven't changed indicates the user has reapplied the original material, in which case overrides should be dropped)
    // otherwise, should only null out the render material where ids or overrides have changed
    //  (the case where ids have changed but overrides are still present is from unsynchronized updates from the simulator, or synchronized
    //  updates with solely transform overrides)

    llassert(!update_server || local_origin);

    S32 start_idx = 0;
    S32 end_idx = getNumTEs();

    if (te_in != -1)
    {
        start_idx = te_in;
        end_idx = start_idx + 1;
    }

    start_idx = llmax(start_idx, 0);
    end_idx = llmin(end_idx, (S32) getNumTEs());

    LLRenderMaterialParams* param_block = (LLRenderMaterialParams*)getParameterEntry(LLNetworkData::PARAMS_RENDER_MATERIAL);
    if (!param_block && id.notNull())
    { // block doesn't exist, but it will need to
        param_block = (LLRenderMaterialParams*)createNewParameterEntry(LLNetworkData::PARAMS_RENDER_MATERIAL)->data;
    }


    LLFetchedGLTFMaterial* new_material = nullptr;
    if (id.notNull())
    {
        new_material = gGLTFMaterialList.getMaterial(id);
    }

    // update local state
    for (S32 te = start_idx; te < end_idx; ++te)
    {
        LLTextureEntry* tep = getTE(te);

        // If local_origin=false (i.e. it's from the server), we know the
        // material has updated or been created, because extra params are
        // checked for equality on unpacking. In that case, checking the
        // material ID for inequality won't work, because the material ID has
        // already been set.
        bool material_changed = !local_origin || !param_block || id != param_block->getMaterial(te);

        if (update_server)
        {
            // Clear most overrides so the render material better matches the material
            // ID (preserve transforms). If overrides become passthrough, set the overrides
            // to nullptr.
            if (tep->setBaseMaterial())
            {
                material_changed = true;
            }
        }

        if (update_server || material_changed)
        {
            tep->setGLTFRenderMaterial(nullptr);
        }

        if (new_material != tep->getGLTFMaterial())
        {
            tep->setGLTFMaterial(new_material, !update_server);
        }

        if (material_changed && new_material)
        {
            // Sometimes, the material may change out from underneath the overrides.
            // This is usually due to the server sending a new material ID, but
            // the overrides have not changed due to being only texture
            // transforms. Re-apply the overrides to the render material here,
            // if present.
            const LLGLTFMaterial* override_material = tep->getGLTFMaterialOverride();
            if (override_material)
            {
                new_material->onMaterialComplete([obj_id = getID(), te]()
                    {
                        LLViewerObject* obj = gObjectList.findObject(obj_id);
                        if (!obj) { return; }
                        LLTextureEntry* tep = obj->getTE(te);
                        if (!tep) { return; }
                        const LLGLTFMaterial* new_material = tep->getGLTFMaterial();
                        if (!new_material) { return; }
                        const LLGLTFMaterial* override_material = tep->getGLTFMaterialOverride();
                        if (!override_material) { return; }
                        LLGLTFMaterial* render_material = new LLFetchedGLTFMaterial();
                        *render_material = *new_material;
                        render_material->applyOverride(*override_material);
                        tep->setGLTFRenderMaterial(render_material);
                    });
            }
        }
    }

    // signal to render pipe that render batches must be rebuilt for this object
    if (!new_material)
    {
        rebuildMaterial();
    }
    else
    {
        new_material->onMaterialComplete([obj_id = getID()]()
            {
                LLViewerObject* obj = gObjectList.findObject(obj_id);
                if (obj)
                {
                    obj->rebuildMaterial();
                }
            });
    }

    // predictively update LLRenderMaterialParams (don't wait for server)
    if (param_block)
    { // update existing parameter block
        for (S32 te = start_idx; te < end_idx; ++te)
        {
            param_block->setMaterial(te, id);
        }
    }

    if (update_server)
    {
        // update via ModifyMaterialParams cap (server will echo back changes)
        for (S32 te = start_idx; te < end_idx; ++te)
        {
            // This sends a cleared version of this object's current material
            // override, but the override should already be cleared due to
            // calling setBaseMaterial above.
            LLGLTFMaterialList::queueApply(this, te, id);
        }
    }

    if (!update_server)
    {
        // Land impact may have changed
        setObjectCostStale();
    }
}

void LLViewerObject::setRenderMaterialIDs(const LLUUID& id)
{
    setRenderMaterialID(-1, id);
}

void LLViewerObject::setRenderMaterialIDs(const LLRenderMaterialParams* material_params, bool local_origin)
{
    if (!local_origin)
    {
        for (S32 te = 0; te < getNumTEs(); ++te)
        {
            const LLUUID& id = material_params ? material_params->getMaterial(te) : LLUUID::null;
            // We know material_params has updated or been created, because
            // extra params are checked for equality on unpacking.
            setRenderMaterialID(te, id, false, false);
        }
    }
}

void LLViewerObject::shrinkWrap()
{
    if (!mShouldShrinkWrap)
    {
        mShouldShrinkWrap = true;
        if (mDrawable)
        { // we weren't shrink wrapped before but we are now, update the spatial partition
            gPipeline.markPartitionMove(mDrawable);
        }
    }
}

void LLViewerObject::setGLTFAsset(const LLUUID& id)
{
    //get the sculpt params and set the sculpt type and id
    auto* param = getExtraParameterEntryCreate(LLNetworkData::PARAMS_SCULPT);

    LLSculptParams* sculpt_params = (LLSculptParams*)param->data;
    sculpt_params->setSculptTexture(id, LL_SCULPT_TYPE_GLTF);

    setParameterEntryInUse(LLNetworkData::PARAMS_SCULPT, true, true);

    // Update the volume
    LLVolumeParams volume_params;
    volume_params.setSculptID(id, LL_SCULPT_TYPE_GLTF);
    updateVolume(volume_params);
}

void LLViewerObject::clearTEWaterExclusion(const U8 te)
{
    if (permModify())
    {
        LLViewerTexture* image = getTEImage(te);
        if (image && (IMG_ALPHA_GRAD == image->getID()))
        {
            // reset texture to default plywood
            setTEImage(te, LLViewerTextureManager::getFetchedTexture(DEFAULT_OBJECT_TEXTURE, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE));

            // reset texture repeats, that might be altered by invisiprim script from wiki
            U32 s_axis, t_axis;
            if (!LLPrimitive::getTESTAxes(te, &s_axis, &t_axis))
            {
                return;
            }
            F32 DEFAULT_REPEATS = 2.f;
            F32 new_s = getScale().mV[s_axis] * DEFAULT_REPEATS;
            F32 new_t = getScale().mV[t_axis] * DEFAULT_REPEATS;

            setTEScale(te, new_s, new_t);
            sendTEUpdate();
        }
    }
}

class ObjectPhysicsProperties : public LLHTTPNode
{
public:
    virtual void post(
        ResponsePtr responder,
        const LLSD& context,
        const LLSD& input) const
    {
        LLSD object_data = input["body"]["ObjectData"];
        S32 num_entries = static_cast<S32>(object_data.size());

        for ( S32 i = 0; i < num_entries; i++ )
        {
            LLSD& curr_object_data = object_data[i];
            U32 local_id = curr_object_data["LocalID"].asInteger();

            // Iterate through nodes at end, since it can be on both the regular AND hover list
            struct f : public LLSelectedNodeFunctor
            {
                U32 mID;
                f(const U32& id) : mID(id) {}
                virtual bool apply(LLSelectNode* node)
                {
                    return (node->getObject() && node->getObject()->mLocalID == mID );
                }
            } func(local_id);

            LLSelectNode* node = LLSelectMgr::getInstance()->getSelection()->getFirstNode(&func);

            if (node)
            {
                // The LLSD message builder doesn't know how to handle U8, so we need to send as S8 and cast
                U8 type = (U8)curr_object_data["PhysicsShapeType"].asInteger();
                F32 density = (F32)curr_object_data["Density"].asReal();
                F32 friction = (F32)curr_object_data["Friction"].asReal();
                F32 restitution = (F32)curr_object_data["Restitution"].asReal();
                F32 gravity = (F32)curr_object_data["GravityMultiplier"].asReal();

                node->getObject()->setPhysicsShapeType(type);
                node->getObject()->setPhysicsGravity(gravity);
                node->getObject()->setPhysicsFriction(friction);
                node->getObject()->setPhysicsDensity(density);
                node->getObject()->setPhysicsRestitution(restitution);
            }
        }

        dialog_refresh_all();
    };
};

LLHTTPRegistration<ObjectPhysicsProperties>
    gHTTPRegistrationObjectPhysicsProperties("/message/ObjectPhysicsProperties");

