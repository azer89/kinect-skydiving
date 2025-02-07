
#include "Stdafx.h"
#include "PlanetObjects.h"

//------------------------------------------------------------------------------------
PlanetObjects::PlanetObjects(void)
{
	targetRadius = 0.0f;
}

//------------------------------------------------------------------------------------
PlanetObjects::~PlanetObjects(void)
{
}

//------------------------------------------------------------------------------------
Ogre::AnimationState* PlanetObjects::setup(Ogre::SceneManager* mSceneManager, RayCastCollision* collisionDetector)
{	
	Ogre::AnimationState* mAni;
	this->mSceneManager = mSceneManager;	
	this->mMainNode = mSceneManager->getRootSceneNode()->createChildSceneNode();

	Ogre::DotSceneLoader* sceneLoader = new Ogre::DotSceneLoader();
	sceneLoader->parseDotScene(GameConfig::getSingletonPtr()->getVillageSceneName(), "Popular", mSceneManager, mMainNode);

	std::vector<Ogre::SceneNode*> nodeList = sceneLoader->nodeList;

	Ogre::Vector3 scaleVect = GameConfig::getSingletonPtr()->getPlanetObjectScaling();
	Ogre::Vector3 transVect = GameConfig::getSingletonPtr()->getPlanetObjectTranslation();
	
	for(int a = 0; a < nodeList.size(); a++)
	{
		Ogre::SceneNode* childNode = nodeList[a];
		Ogre::MovableObject* object = childNode->getAttachedObject(0);
		object->setCastShadows(true);

		Ogre::Vector3 pos = childNode->_getDerivedPosition();

		Ogre::Vector3 tempPos((pos.x * scaleVect.x) + transVect.x, 
							 transVect.y, 
							 (pos.z * scaleVect.z) + transVect.z);

		Ogre::Vector3 dirVector = tempPos.normalisedCopy();
		childNode->setScale(scaleVect);

		Ogre::Vector3 actualPos = dirVector * 5000;
		
		Ogre::Vector3 intersection = Ogre::Vector3::ZERO;
		collisionDetector->getChunksIntersection(tempPos, -dirVector, intersection);

		if(intersection.x == 0 && intersection.y == 5000 && intersection.z == 0)	// ray intersection fails
		{
			childNode->setPosition(actualPos);	// guess the terrain height is 2000 from planet's center
		}
		else	// ray intersection is obtained
		{
			childNode->setPosition(intersection);
			actualPos = intersection;
		}

		Ogre::Vector3 upVector = tempPos.normalisedCopy();
		Ogre::Quaternion q = Ogre::Vector3::UNIT_Y.getRotationTo(upVector);
		childNode->setOrientation(q);

		if (childNode->getName() == "object53")
		{
			targetRadius = object->getBoundingRadius();

			signNode = mSceneManager->getRootSceneNode()->createChildSceneNode();
			signEntity = mSceneManager->createEntity("sign.mesh");
			signNode->attachObject(signEntity);
			signNode->setPosition(actualPos);
			signNode->setScale(1,1,1);
			signNode->setOrientation(q);

			mAni = signEntity->getAnimationState("go");
			mAni->setEnabled(true);
			mAni->setLoop(true);
		}
	}

	delete sceneLoader;
	return mAni;
}
