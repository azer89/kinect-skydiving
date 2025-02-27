
#include "Stdafx.h"
#include "Atmosphere.h"
#include "PlanetMath.h"


//using namespace Ogre;

namespace GalaxyEngine
{
	Atmosphere::Atmosphere(float radius, Ogre::uint32 highRes, Ogre::uint32 mediumRes, Ogre::uint32 lowRes, float maxPixelError, const Ogre::String &atmosphereMap)
		: withinFarDistance(false),
		minDistanceSquared(0),
		bestTechnique(NULL),
		sunLight(NULL)
	{
		mName = "Atmosphere";
		Atmosphere::radius = radius;
		bounds.setMinimum(-radius, -radius, -radius);
		bounds.setMaximum(radius, radius, radius);

		material = Ogre::MaterialManager::getSingleton().create(Utility::getUniqueID(), Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass *pass = material->getTechnique(0)->getPass(0);
		//pass->createTextureUnitState(atmosphereMap);
		pass->setVertexProgram("Atmosphere_vs");
		pass->setFragmentProgram("Atmosphere_ps");

		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthCheckEnabled(true);
		pass->setDepthWriteEnabled(false);
		pass->setCullingMode(Ogre::CULL_NONE);		// Make nice blue sky
		//pass->isTransparent(true);

		Atmosphere::maxPixelError = maxPixelError;
		lowMesh = new AtmosphereRenderable(this, lowRes);
		mediumMesh = new AtmosphereRenderable(this, mediumRes);
		highMesh = new AtmosphereRenderable(this, highRes);

		lowMeshError = ((0.5f * Ogre::Math::PI * radius) / (float)lowRes);
		mediumMeshError = ((0.5f * Ogre::Math::PI * radius) / (float)mediumRes);
		highMeshError = ((0.5f * Ogre::Math::PI * radius) / (float)highRes);
	}

	Atmosphere::~Atmosphere()
	{
		delete lowMesh;
		delete mediumMesh;
		delete highMesh;
	}

	void Atmosphere::_notifyCurrentCamera(Ogre::Camera *cam)
	{
		//Calculate camera distance
		camPos = cam->getDerivedPosition();
		Ogre::Vector3 camVec = camPos - getParentSceneNode()->_getDerivedPosition();
		Ogre::Real centerDistanceSquared = camVec.squaredLength();

		//withinFarDistance = true;
		
		if (getRenderingDistance() == 0) {
			withinFarDistance = true;
		} else {
			minDistanceSquared = std::max(0.0f, centerDistanceSquared - (radius * radius));
			//Note: centerDistanceSquared measures the distance between the camera and the center of the atmosphere,
			//while minDistanceSquared measures the closest distance between the camera and the closest vertex of
			//the atmosphere.

			//Determine whether the atmosphere is within the far rendering distance
			withinFarDistance = minDistanceSquared <= Ogre::Math::Sqr(getRenderingDistance());
		}
		
		//Calculate the best material technique
		bestTechnique = material->getBestTechnique(material->getLodIndex(centerDistanceSquared));
		if (!bestTechnique){
			material->load();
			bestTechnique = material->getBestTechnique(material->getLodIndex(centerDistanceSquared));
		}

		//Scale mesh appropriately
		getParentSceneNode()->setScale(radius, radius, radius);

		//Update the atmospheric scattering calculations
		if (sunLight) 
		{
			updateLighting(sunLight, cam, getParentSceneNode());
		}

        //Calculate the "perspective scaling factor" (used in LOD calculations)
        float viewportHeight = cam->getViewport()->getActualHeight();
        perspectiveScalingFactor = viewportHeight / (2 * Ogre::Math::Tan(cam->getFOVy()/2));
	}

	void Atmosphere::updateLighting(Ogre::Light *light, Ogre::Camera *cam, Ogre::SceneNode *node)
	{		
		Ogre::Vector3 lightDir = light->getDerivedPosition() - node->_getDerivedPosition();
		lightDir.normalise();

		Ogre::Vector3 relativeCameraPos = cam->getDerivedPosition() - getParentSceneNode()->_getDerivedPosition();
		relativeCameraPos /= radius;

		Ogre::Pass *pass = material->getTechnique(0)->getPass(0);
		Ogre::GpuProgramParametersSharedPtr vsParams = pass->getVertexProgramParameters();
		Ogre::GpuProgramParametersSharedPtr psParams = pass->getFragmentProgramParameters();
		vsParams->setNamedConstant("lightDirection", lightDir);
		psParams->setNamedConstant("relativeCameraPos", relativeCameraPos);
	}

	bool Atmosphere::isVisible()
	{
		return mVisible && withinFarDistance;
	}

	void Atmosphere::_updateRenderQueue(Ogre::RenderQueue *queue)
	{
		//Render
        float distance = std::max(0.00001f, getParentSceneNode()->_getDerivedPosition().distance(camPos) - radius);

		if ((perspectiveScalingFactor * lowMeshError / distance) <= maxPixelError)
			queue->addRenderable(lowMesh);
		else if ((perspectiveScalingFactor * mediumMeshError / distance) <= maxPixelError)
			queue->addRenderable(mediumMesh);
		else
			queue->addRenderable(highMesh);
	}


	Atmosphere::AtmosphereRenderable::AtmosphereRenderable(Atmosphere *atmosphere, Ogre::uint32 resolution)
	{
		//Save misc. variables
		this->atmosphere = atmosphere;
		float invRes = 1.0f / resolution;

		//Load vertex buffer
		Ogre::uint32 vertCount = 6 * (resolution+1) * (resolution+1);
		assert(vertCount < 0xFFFF);
		size_t offset = 0;
		vertexData.vertexDeclaration->addElement(0, offset, Ogre::VET_FLOAT3, Ogre::VES_POSITION);
		offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
		//vertexData.vertexDeclaration->addElement(0, offset, VET_FLOAT2, VES_TEXTURE_COORDINATES);
		Ogre::HardwareVertexBufferSharedPtr vertBuff = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
			vertexData.vertexDeclaration->getVertexSize(0), vertCount, Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY, false);

		float *vBuff = static_cast<float*>(vertBuff->lock(Ogre::HardwareBuffer::HBL_DISCARD));

		for (Ogre::uint32 face = 0; face < 6; ++face) {
			float faceNormalOffsetU = (face % 3) / 4.0f;
			float faceNormalOffsetV = (face / 3) / 2.0f;

			for (Ogre::uint32 y = 0; y <= resolution; ++y){
				for (Ogre::uint32 x = 0; x <= resolution; ++x){
					//Calculate UVs and terrain height
					Ogre::Real tx = x * invRes;
					Ogre::Real ty = y * invRes;

					//Calculate vertex position / normal
					Ogre::Vector3 pos = PlanetMath::mapCubeToUnitSphere(mapPlaneToCube(tx, ty, (PlanetMath::CubeFace)face));

					//Add vertex
					*vBuff++ = pos.x; *vBuff++ = pos.y; *vBuff++ = pos.z;
					//*vBuff++ = faceNormalOffsetU + tx * 0.25f; *vBuff++ = faceNormalOffsetV + ty * 0.5f;
				}
			}
		}

		vertBuff->unlock();
		vertexData.vertexStart = 0;
		vertexData.vertexCount = vertCount;
		vertexData.vertexBufferBinding->setBinding(0, vertBuff);

		//Load index buffer
		Ogre::uint32 indexCount = 6 * 6 * resolution * resolution;
		Ogre::HardwareIndexBufferSharedPtr indexBuff = Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
			Ogre::HardwareIndexBuffer::IT_16BIT, indexCount, Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY, false);

		Ogre::uint16 *iBuff = static_cast<Ogre::uint16*>(indexBuff->lock(Ogre::HardwareBuffer::HBL_DISCARD));

		//Index the chunk grid
		for (Ogre::uint32 face = 0; face < 6; ++face) {
			Ogre::uint32 faceOffset = face * ((resolution+1) * (resolution+1));
			for (Ogre::uint32 y = 0; y < resolution; ++y){
				for (Ogre::uint32 x = 0; x < resolution; ++x){
					Ogre::uint16 vTopLeft = faceOffset + (y * (resolution+1) + x);
					Ogre::uint16 vBottomLeft = faceOffset + ((y+1) * (resolution+1) + x);
					*iBuff++ = vBottomLeft;
					*iBuff++ = vTopLeft+1;
					*iBuff++ = vTopLeft;
					*iBuff++ = vBottomLeft;
					*iBuff++ = vBottomLeft+1;
					*iBuff++ = vTopLeft+1;
				}
			}
		}

		indexBuff->unlock();
		indexData.indexStart = 0;
		indexData.indexCount = indexCount;
		indexData.indexBuffer = indexBuff;
	}

	Atmosphere::AtmosphereRenderable::~AtmosphereRenderable()
	{
		vertexData.vertexStart = 0;
		vertexData.vertexCount = 0;
		vertexData.vertexDeclaration->removeAllElements();
		vertexData.vertexBufferBinding->unsetAllBindings();

		indexData.indexStart = 0;
		indexData.indexCount = 0;
		indexData.indexBuffer.setNull();
	}

	void Atmosphere::AtmosphereRenderable::getRenderOperation(Ogre::RenderOperation& op)
	{
		op.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST;
		op.srcRenderable = this;
		op.useIndexes = true;
		op.vertexData = &vertexData;
		op.indexData = &indexData;
	}

	Ogre::Real Atmosphere::AtmosphereRenderable::getSquaredViewDepth(const Ogre::Camera* cam) const
	{
		Ogre::Vector3 camVec = cam->getDerivedPosition() - atmosphere->getParentSceneNode()->_getDerivedPosition();
		return camVec.squaredLength();
	}

	const Ogre::LightList& Atmosphere::AtmosphereRenderable::getLights(void) const
	{
		return atmosphere->queryLights();
	}

}
