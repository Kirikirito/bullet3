/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2011 Advanced Micro Devices, Inc.  http://bulletphysics.org

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/


///This file was written by Erwin Coumans
///Separating axis rest based on work from Pierre Terdiman, see
///And contact clipping based on work from Simon Hobbs

//#define BT_DEBUG_SAT_FACE

#include "ConvexHullContact.h"
#include <string.h>//memcpy
#include "btConvexPolyhedronCL.h"


typedef btAlignedObjectArray<btVector3> btVertexArray;
#include "BulletCommon/btQuickprof.h"

#include <float.h> //for FLT_MAX
#include "basic_initialize/btOpenCLUtils.h"
#include "parallel_primitives/host/btLauncherCL.h"
//#include "AdlQuaternion.h"

#include "../kernels/satKernels.h"
#include "../kernels/satClipHullContacts.h"
#include "../kernels/bvhTraversal.h"
#include "../kernels/primitiveContacts.h"

#include "BulletGeometry/btAabbUtil2.h"


#define dot3F4 btDot

GpuSatCollision::GpuSatCollision(cl_context ctx,cl_device_id device, cl_command_queue  q )
:m_context(ctx),
m_device(device),
m_queue(q),
m_findSeparatingAxisKernel(0),
m_totalContactsOut(m_context, m_queue)
{
	m_totalContactsOut.push_back(0);
	
	cl_int errNum=0;
	bool disableKernelCaching = true;

	if (1)
	{
		const char* src = satKernelsCL;
		cl_program satProg = btOpenCLUtils::compileCLProgramFromString(m_context,m_device,src,&errNum,"","opencl/gpu_sat/kernels/sat.cl");
		btAssert(errNum==CL_SUCCESS);

		m_findSeparatingAxisKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,src, "findSeparatingAxisKernel",&errNum,satProg );

		m_findConcaveSeparatingAxisKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,src, "findConcaveSeparatingAxisKernel",&errNum,satProg );


		m_findCompoundPairsKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,src, "findCompoundPairsKernel",&errNum,satProg );
	
		m_processCompoundPairsKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,src, "processCompoundPairsKernel",&errNum,satProg );
		btAssert(errNum==CL_SUCCESS);
	}

	if (1)
	{
		const char* srcClip = satClipKernelsCL;
		cl_program satClipContactsProg = btOpenCLUtils::compileCLProgramFromString(m_context,m_device,srcClip,&errNum,"","opencl/gpu_sat/kernels/satClipHullContacts.cl");
		btAssert(errNum==CL_SUCCESS);

		m_clipHullHullKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "clipHullHullKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);

		m_clipCompoundsHullHullKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "clipCompoundsHullHullKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);
		

        m_findClippingFacesKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "findClippingFacesKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);

        m_clipFacesAndContactReductionKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "clipFacesAndContactReductionKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);        

		m_clipHullHullConcaveConvexKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "clipHullHullConcaveConvexKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);

		m_extractManifoldAndAddContactKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip, "extractManifoldAndAddContactKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);

        m_newContactReductionKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcClip,
                            "newContactReductionKernel",&errNum,satClipContactsProg);
		btAssert(errNum==CL_SUCCESS);
	}
   else
	{
		m_clipHullHullKernel=0;
		m_clipCompoundsHullHullKernel = 0;
        m_findClippingFacesKernel = 0;
        m_newContactReductionKernel=0;
        m_clipFacesAndContactReductionKernel = 0;
		m_clipHullHullConcaveConvexKernel = 0;
		m_extractManifoldAndAddContactKernel = 0;
	}

	 if (1)
	{
		const char* srcBvh = bvhTraversalKernelCL;
		cl_program bvhTraversalProg = btOpenCLUtils::compileCLProgramFromString(m_context,m_device,srcBvh,&errNum,"","opencl/gpu_sat/kernels/bvhTraversal.cl");
		//cl_program bvhTraversalProg = btOpenCLUtils::compileCLProgramFromString(m_context,m_device,0,&errNum,"","opencl/gpu_sat/kernels/bvhTraversal.cl", true);
		btAssert(errNum==CL_SUCCESS);

		m_bvhTraversalKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,srcBvh, "bvhTraversalKernel",&errNum,bvhTraversalProg,"");
		btAssert(errNum==CL_SUCCESS);

	}
        
	 {
		 const char* primitiveContactsSrc = primitiveContactsKernelsCL;
		cl_program primitiveContactsProg = btOpenCLUtils::compileCLProgramFromString(m_context,m_device,primitiveContactsSrc,&errNum,"","opencl/gpu_sat/kernels/primitiveContacts.cl",disableKernelCaching);
		btAssert(errNum==CL_SUCCESS);

		m_primitiveContactsKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,primitiveContactsSrc, "primitiveContactsKernel",&errNum,primitiveContactsProg,"");
		btAssert(errNum==CL_SUCCESS);

		m_processCompoundPairsPrimitivesKernel = btOpenCLUtils::compileCLKernelFromString(m_context, m_device,primitiveContactsSrc, "processCompoundPairsPrimitivesKernel",&errNum,primitiveContactsProg,"");
		btAssert(errNum==CL_SUCCESS);
		btAssert(m_processCompoundPairsPrimitivesKernel);
		 
	 }
	

}

GpuSatCollision::~GpuSatCollision()
{
	
	if (m_findSeparatingAxisKernel)
		clReleaseKernel(m_findSeparatingAxisKernel);

	if (m_findConcaveSeparatingAxisKernel)
		clReleaseKernel(m_findConcaveSeparatingAxisKernel);

	if (m_findCompoundPairsKernel)
		clReleaseKernel(m_findCompoundPairsKernel);

	if (m_processCompoundPairsKernel)
		clReleaseKernel(m_processCompoundPairsKernel);
    
    if (m_findClippingFacesKernel)
        clReleaseKernel(m_findClippingFacesKernel);
   
    if (m_clipFacesAndContactReductionKernel)
        clReleaseKernel(m_clipFacesAndContactReductionKernel);
    if (m_newContactReductionKernel)
        clReleaseKernel(m_newContactReductionKernel);
	if (m_primitiveContactsKernel)
		clReleaseKernel(m_primitiveContactsKernel);
    
	if (m_processCompoundPairsPrimitivesKernel)
		clReleaseKernel(m_processCompoundPairsPrimitivesKernel);

	if (m_clipHullHullKernel)
		clReleaseKernel(m_clipHullHullKernel);
	if (m_clipCompoundsHullHullKernel)
		clReleaseKernel(m_clipCompoundsHullHullKernel);

	if (m_clipHullHullConcaveConvexKernel)
		clReleaseKernel(m_clipHullHullConcaveConvexKernel);
	if (m_extractManifoldAndAddContactKernel)
		clReleaseKernel(m_extractManifoldAndAddContactKernel);

	if (m_bvhTraversalKernel)
		clReleaseKernel(m_bvhTraversalKernel);

}

struct MyTriangleCallback : public btNodeOverlapCallback
{
	int m_bodyIndexA;
	int m_bodyIndexB;

	virtual void processNode(int subPart, int triangleIndex)
	{
		printf("bodyIndexA %d, bodyIndexB %d\n",m_bodyIndexA,m_bodyIndexB);
		printf("triangleIndex %d\n", triangleIndex);
	}
};


#define float4 btVector3
#define make_float4(x,y,z,w) btVector4(x,y,z,w)

float signedDistanceFromPointToPlane(const float4& point, const float4& planeEqn, float4* closestPointOnFace)
{
	float4 n = planeEqn;
	n[3] = 0.f;
	float dist = dot3F4(n, point) + planeEqn[3];
	*closestPointOnFace = point - dist * n;
	return dist;
}



#define cross3(a,b) (a.cross(b))
btVector3 transform(btVector3* v, const btVector3* pos, const btVector3* orn)
{
	btTransform tr;
	tr.setIdentity();
	tr.setOrigin(*pos);
	btQuaternion* o = (btQuaternion*) orn;
	tr.setRotation(*o);
	btVector3 res = tr(*v);
	return res;
}


inline bool IsPointInPolygon(const float4& p, 
							const btGpuFace* face,
							 const float4* baseVertex,
							const  int* convexIndices,
							float4* out)
{
    float4 a;
    float4 b;
    float4 ab;
    float4 ap;
    float4 v;

	float4 plane = make_float4(face->m_plane.x,face->m_plane.y,face->m_plane.z,0.f);
	
	if (face->m_numIndices<2)
		return false;

	
	float4 v0 = baseVertex[convexIndices[face->m_indexOffset + face->m_numIndices-1]];
	b = v0;

    for(unsigned i=0; i != face->m_numIndices; ++i)
    {
		a = b;
		float4 vi = baseVertex[convexIndices[face->m_indexOffset + i]];
		b = vi;
        ab = b-a;
        ap = p-a;
        v = cross3(ab,plane);

        if (btDot(ap, v) > 0.f)
        {
            float ab_m2 = btDot(ab, ab);
            float rt = ab_m2 != 0.f ? btDot(ab, ap) / ab_m2 : 0.f;
            if (rt <= 0.f)
            {
                *out = a;
            }
            else if (rt >= 1.f) 
            {
                *out = b;
            }
            else
            {
            	float s = 1.f - rt;
				out[0].x = s * a.x + rt * b.x;
				out[0].y = s * a.y + rt * b.y;
				out[0].z = s * a.z + rt * b.z;
            }
            return false;
        }
    }
    return true;
}


void	computeContactSphereConvex(int pairIndex,
																int bodyIndexA, int bodyIndexB, 
																int collidableIndexA, int collidableIndexB, 
																const btRigidBodyCL* rigidBodies, 
																const btCollidable* collidables,
																const btConvexPolyhedronCL* convexShapes,
																const btVector3* convexVertices,
																const int* convexIndices,
																const btGpuFace* faces,
																btContact4* globalContactsOut,
																int& nGlobalContactsOut,
																int maxContactCapacity)
{

	float radius = collidables[collidableIndexA].m_radius;
	float4 spherePos1 = rigidBodies[bodyIndexA].m_pos;
	btQuaternion sphereOrn = rigidBodies[bodyIndexA].m_quat;



	float4 pos = rigidBodies[bodyIndexB].m_pos;
	

	btQuaternion quat = rigidBodies[bodyIndexB].m_quat;

	btTransform tr;
	tr.setIdentity();
	tr.setOrigin(pos);
	tr.setRotation(quat);
	btTransform trInv = tr.inverse();

	float4 spherePos = trInv(spherePos1);

	int collidableIndex = rigidBodies[bodyIndexB].m_collidableIdx;
	int shapeIndex = collidables[collidableIndex].m_shapeIndex;
	int numFaces = convexShapes[shapeIndex].m_numFaces;
	float4 closestPnt = make_float4(0, 0, 0, 0);
	float4 hitNormalWorld = make_float4(0, 0, 0, 0);
	float minDist = -1000000.f; // TODO: What is the largest/smallest float?
	bool bCollide = true;
	int region = -1;
	float4 localHitNormal;
	for ( int f = 0; f < numFaces; f++ )
	{
		btGpuFace face = faces[convexShapes[shapeIndex].m_faceOffset+f];
		float4 planeEqn;
		float4 localPlaneNormal = make_float4(face.m_plane.getX(),face.m_plane.getY(),face.m_plane.getZ(),0.f);
		float4 n1 = localPlaneNormal;//quatRotate(quat,localPlaneNormal);
		planeEqn = n1;
		planeEqn[3] = face.m_plane[3];

		float4 pntReturn;
		float dist = signedDistanceFromPointToPlane(spherePos, planeEqn, &pntReturn);

		if ( dist > radius)
		{
			bCollide = false;
			break;
		}

		if ( dist > 0 )
		{
			//might hit an edge or vertex
			btVector3 out;

			bool isInPoly = IsPointInPolygon(spherePos,
					&face,
					&convexVertices[convexShapes[shapeIndex].m_vertexOffset],
					convexIndices,
                    &out);
			if (isInPoly)
			{
				if (dist>minDist)
				{
					minDist = dist;
					closestPnt = pntReturn;
					localHitNormal = planeEqn;
					region=1;
				}
			} else
			{
				btVector3 tmp = spherePos-out;
				btScalar l2 = tmp.length2();
				if (l2<radius*radius)
				{
					dist  = btSqrt(l2);
					if (dist>minDist)
					{
						minDist = dist;
						closestPnt = out;
						localHitNormal = tmp/dist;
						region=2;
					}
					
				} else
				{
					bCollide = false;
					break;
				}
			}
		}
		else
		{
			if ( dist > minDist )
			{
				minDist = dist;
				closestPnt = pntReturn;
				localHitNormal = planeEqn;
				region=3;
			}
		}
	}
	static int numChecks = 0;
	numChecks++;

	if (bCollide && minDist > -10000)
	{
		
		float4 normalOnSurfaceB1 = tr.getBasis()*-localHitNormal;//-hitNormalWorld;
		float4 pOnB1 = tr(closestPnt);
		//printf("dist ,%f,",minDist);
		float actualDepth = minDist-radius;
		if (actualDepth<0)
		{
		//printf("actualDepth = ,%f,", actualDepth);
		//printf("normalOnSurfaceB1 = ,%f,%f,%f,", normalOnSurfaceB1.getX(),normalOnSurfaceB1.getY(),normalOnSurfaceB1.getZ());
		//printf("region=,%d,\n", region);
		pOnB1[3] = actualDepth;

		int dstIdx;
//    dstIdx = nGlobalContactsOut++;//AppendInc( nGlobalContactsOut, dstIdx );
		
		if (nGlobalContactsOut < maxContactCapacity)
		{
			dstIdx=nGlobalContactsOut;
			nGlobalContactsOut++;

			btContact4* c = &globalContactsOut[dstIdx];
			c->m_worldNormal = normalOnSurfaceB1;
			c->setFrictionCoeff(0.7);
			c->setRestituitionCoeff(0.f);

			c->m_batchIdx = pairIndex;
			c->m_bodyAPtrAndSignBit = rigidBodies[bodyIndexA].m_invMass==0?-bodyIndexA:bodyIndexA;
			c->m_bodyBPtrAndSignBit = rigidBodies[bodyIndexB].m_invMass==0?-bodyIndexB:bodyIndexB;
			c->m_worldPos[0] = pOnB1;
			int numPoints = 1;
			c->m_worldNormal[3] = numPoints;
		}//if (dstIdx < numPairs)
		}
	}//if (hasCollision)
	
}


void GpuSatCollision::computeConvexConvexContactsGPUSAT( const btOpenCLArray<btInt2>* pairs, int nPairs,
			const btOpenCLArray<btRigidBodyCL>* bodyBuf,
			btOpenCLArray<btContact4>* contactOut, int& nContacts,
			int maxContactCapacity,
			const btOpenCLArray<btConvexPolyhedronCL>& convexData,
			const btOpenCLArray<btVector3>& gpuVertices,
			const btOpenCLArray<btVector3>& gpuUniqueEdges,
			const btOpenCLArray<btGpuFace>& gpuFaces,
			const btOpenCLArray<int>& gpuIndices,
			const btOpenCLArray<btCollidable>& gpuCollidables,
			const btOpenCLArray<btGpuChildShape>& gpuChildShapes,

			const btOpenCLArray<btYetAnotherAabb>& clAabbsWS,
            btOpenCLArray<btVector3>& worldVertsB1GPU,
            btOpenCLArray<btInt4>& clippingFacesOutGPU,
            btOpenCLArray<btVector3>& worldNormalsAGPU,
            btOpenCLArray<btVector3>& worldVertsA1GPU,
            btOpenCLArray<btVector3>& worldVertsB2GPU,    
			btAlignedObjectArray<class btOptimizedBvh*>& bvhData,
			btOpenCLArray<btQuantizedBvhNode>*	treeNodesGPU,
			btOpenCLArray<btBvhSubtreeInfo>*	subTreesGPU,
			int numObjects,
			int maxTriConvexPairCapacity,
			btOpenCLArray<btInt4>& triangleConvexPairsOut,
			int& numTriConvexPairsOut
			)
{
	if (!nPairs)
		return;


//#define CHECK_ON_HOST
#ifdef CHECK_ON_HOST
	btAlignedObjectArray<btYetAnotherAabb> hostAabbs;
	clAabbsWS.copyToHost(hostAabbs);
	btAlignedObjectArray<btInt2> hostPairs;
	pairs->copyToHost(hostPairs);

	btAlignedObjectArray<btRigidBodyCL> hostBodyBuf;
	bodyBuf->copyToHost(hostBodyBuf);

	

	btAlignedObjectArray<btConvexPolyhedronCL> hostConvexData;
	convexData.copyToHost(hostConvexData);

	btAlignedObjectArray<btVector3> hostVertices;
	gpuVertices.copyToHost(hostVertices);

	btAlignedObjectArray<btVector3> hostUniqueEdges;
	gpuUniqueEdges.copyToHost(hostUniqueEdges);
	btAlignedObjectArray<btGpuFace> hostFaces;
	gpuFaces.copyToHost(hostFaces);
	btAlignedObjectArray<int> hostIndices;
	gpuIndices.copyToHost(hostIndices);
	btAlignedObjectArray<btCollidable> hostCollidables;
	gpuCollidables.copyToHost(hostCollidables);
	
	btAlignedObjectArray<btGpuChildShape> cpuChildShapes;
	gpuChildShapes.copyToHost(cpuChildShapes);
	

	btAlignedObjectArray<btInt4> hostTriangleConvexPairs;

	btAlignedObjectArray<btContact4> hostContacts;
	if (nContacts)
	{
		contactOut->copyToHost(hostContacts);
	}

	hostContacts.resize(nPairs);

	for (int i=0;i<nPairs;i++)
	{
		int bodyIndexA = hostPairs[i].x;
		int bodyIndexB = hostPairs[i].y;
		int collidableIndexA = hostBodyBuf[bodyIndexA].m_collidableIdx;
		int collidableIndexB = hostBodyBuf[bodyIndexB].m_collidableIdx;

		if (hostCollidables[collidableIndexA].m_shapeType == SHAPE_SPHERE &&
			hostCollidables[collidableIndexB].m_shapeType == SHAPE_CONVEX_HULL)
		{
			computeContactSphereConvex(i,bodyIndexA,bodyIndexB,collidableIndexA,collidableIndexB,&hostBodyBuf[0],
				&hostCollidables[0],&hostConvexData[0],&hostVertices[0],&hostIndices[0],&hostFaces[0],&hostContacts[0],nContacts,nPairs);
		}

		if (hostCollidables[collidableIndexA].m_shapeType == SHAPE_CONVEX_HULL &&
			hostCollidables[collidableIndexB].m_shapeType == SHAPE_SPHERE)
		{
			computeContactSphereConvex(i,bodyIndexB,bodyIndexA,collidableIndexB,collidableIndexA,&hostBodyBuf[0],
				&hostCollidables[0],&hostConvexData[0],&hostVertices[0],&hostIndices[0],&hostFaces[0],&hostContacts[0],nContacts,nPairs);
			//printf("convex-sphere\n");
			
		}

		
	}

	if (nContacts)
		{
			hostContacts.resize(nContacts);
			contactOut->copyFromHost(hostContacts);
		}

	
#else

	{
		if (nPairs)
		{
			m_totalContactsOut.copyFromHostPointer(&nContacts,1,0,true);

			BT_PROFILE("primitiveContactsKernel");
			btBufferInfoCL bInfo[] = {
				btBufferInfoCL( pairs->getBufferCL(), true ), 
				btBufferInfoCL( bodyBuf->getBufferCL(),true), 
				btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
				btBufferInfoCL( convexData.getBufferCL(),true),
				btBufferInfoCL( gpuVertices.getBufferCL(),true),
				btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
				btBufferInfoCL( gpuFaces.getBufferCL(),true),
				btBufferInfoCL( gpuIndices.getBufferCL(),true),
				btBufferInfoCL( contactOut->getBufferCL()),
				btBufferInfoCL( m_totalContactsOut.getBufferCL())	
			};
			
			btLauncherCL launcher(m_queue, m_primitiveContactsKernel);
			launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst( nPairs  );
			launcher.setConst(maxContactCapacity);
			int num = nPairs;
			launcher.launch1D( num);
			clFinish(m_queue);
		
			nContacts = m_totalContactsOut.at(0);
			contactOut->resize(nContacts);
		}
	}

#endif//CHECK_ON_HOST
	
	BT_PROFILE("computeConvexConvexContactsGPUSAT");
   // printf("nContacts = %d\n",nContacts);
    
	btOpenCLArray<btVector3> sepNormals(m_context,m_queue);
	sepNormals.resize(nPairs);
	btOpenCLArray<int> hasSeparatingNormals(m_context,m_queue);
	hasSeparatingNormals.resize(nPairs);
	
	int concaveCapacity=maxTriConvexPairCapacity;
	btOpenCLArray<btVector3> concaveSepNormals(m_context,m_queue);
	concaveSepNormals.resize(concaveCapacity);

	btOpenCLArray<int> numConcavePairsOut(m_context,m_queue);
	numConcavePairsOut.push_back(0);

	int compoundPairCapacity=65536*10;
	btOpenCLArray<btCompoundOverlappingPair> gpuCompoundPairs(m_context,m_queue);
	gpuCompoundPairs.resize(compoundPairCapacity);

	btOpenCLArray<btVector3> gpuCompoundSepNormals(m_context,m_queue);
	gpuCompoundSepNormals.resize(compoundPairCapacity);
	
	
	btOpenCLArray<int> gpuHasCompoundSepNormals(m_context,m_queue);
	gpuHasCompoundSepNormals.resize(compoundPairCapacity);
	
	btOpenCLArray<int> numCompoundPairsOut(m_context,m_queue);
	numCompoundPairsOut.push_back(0);

	int numCompoundPairs = 0;

	bool findSeparatingAxisOnGpu = true;//false;
	int numConcavePairs =0;

	{
		clFinish(m_queue);
		if (findSeparatingAxisOnGpu)
		{
		
			{
				BT_PROFILE("findSeparatingAxisKernel");
				btBufferInfoCL bInfo[] = { 
					btBufferInfoCL( pairs->getBufferCL(), true ), 
					btBufferInfoCL( bodyBuf->getBufferCL(),true), 
					btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
					btBufferInfoCL( convexData.getBufferCL(),true),
					btBufferInfoCL( gpuVertices.getBufferCL(),true),
					btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
					btBufferInfoCL( gpuFaces.getBufferCL(),true),
					btBufferInfoCL( gpuIndices.getBufferCL(),true),
					btBufferInfoCL( clAabbsWS.getBufferCL(),true),
					btBufferInfoCL( sepNormals.getBufferCL()),
					btBufferInfoCL( hasSeparatingNormals.getBufferCL())
				};

				btLauncherCL launcher(m_queue, m_findSeparatingAxisKernel);
				launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
				launcher.setConst( nPairs  );

				int num = nPairs;
				launcher.launch1D( num);
				clFinish(m_queue);
			}

			//now perform the tree query on GPU
			{
				
				int numNodes = bvhData.size()? bvhData[0]->getQuantizedNodeArray().size() : 0;
				if (numNodes)
				{
					int numSubTrees = subTreesGPU->size();
					btVector3 bvhAabbMin = bvhData[0]->m_bvhAabbMin;
					btVector3 bvhAabbMax = bvhData[0]->m_bvhAabbMax;
					btVector3 bvhQuantization = bvhData[0]->m_bvhQuantization;
					{
						BT_PROFILE("m_bvhTraversalKernel");
						numConcavePairs = numConcavePairsOut.at(0);
						btLauncherCL launcher(m_queue, m_bvhTraversalKernel);
						launcher.setBuffer( pairs->getBufferCL());
						launcher.setBuffer(  bodyBuf->getBufferCL());
						launcher.setBuffer( gpuCollidables.getBufferCL());
						launcher.setBuffer( clAabbsWS.getBufferCL());
						launcher.setBuffer( triangleConvexPairsOut.getBufferCL());
						launcher.setBuffer( numConcavePairsOut.getBufferCL());
						launcher.setBuffer( subTreesGPU->getBufferCL());
						launcher.setBuffer( treeNodesGPU->getBufferCL());
						launcher.setConst( bvhAabbMin);
						launcher.setConst( bvhAabbMax);
						launcher.setConst( bvhQuantization);
						launcher.setConst(numSubTrees);
						launcher.setConst( nPairs  );
						launcher.setConst( maxTriConvexPairCapacity);
						int num = nPairs;
						launcher.launch1D( num);
						clFinish(m_queue);
						numConcavePairs = numConcavePairsOut.at(0);
						
						if (numConcavePairs > maxTriConvexPairCapacity)
						{
							static int exceeded_maxTriConvexPairCapacity_count = 0;
							printf("Rxceeded %d times the maxTriConvexPairCapacity (found %d but max is %d)\n", exceeded_maxTriConvexPairCapacity_count++,
								numConcavePairs,maxTriConvexPairCapacity);
							numConcavePairs = maxTriConvexPairCapacity;
						}
						triangleConvexPairsOut.resize(numConcavePairs);
						if (numConcavePairs)
						{
							//now perform a SAT test for each triangle-convex element (stored in triangleConvexPairsOut)
							BT_PROFILE("findConcaveSeparatingAxisKernel");
							btBufferInfoCL bInfo[] = { 
								btBufferInfoCL( triangleConvexPairsOut.getBufferCL() ), 
								btBufferInfoCL( bodyBuf->getBufferCL(),true), 
								btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
								btBufferInfoCL( convexData.getBufferCL(),true),
								btBufferInfoCL( gpuVertices.getBufferCL(),true),
								btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
								btBufferInfoCL( gpuFaces.getBufferCL(),true),
								btBufferInfoCL( gpuIndices.getBufferCL(),true),
								btBufferInfoCL( clAabbsWS.getBufferCL(),true),
								btBufferInfoCL( sepNormals.getBufferCL()),
								btBufferInfoCL( hasSeparatingNormals.getBufferCL()),
								btBufferInfoCL( concaveSepNormals.getBufferCL())
							};

							btLauncherCL launcher(m_queue, m_findConcaveSeparatingAxisKernel);
							launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );

							launcher.setConst( numConcavePairs  );

							int num = numConcavePairs;
							launcher.launch1D( num);
							clFinish(m_queue);
						}
					}
				}
			}
			

			{
				BT_PROFILE("findCompoundPairsKernel");
				btBufferInfoCL bInfo[] = 
				{ 
					btBufferInfoCL( pairs->getBufferCL(), true ), 
					btBufferInfoCL( bodyBuf->getBufferCL(),true), 
					btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
					btBufferInfoCL( convexData.getBufferCL(),true),
					btBufferInfoCL( gpuVertices.getBufferCL(),true),
					btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
					btBufferInfoCL( gpuFaces.getBufferCL(),true),
					btBufferInfoCL( gpuIndices.getBufferCL(),true),
					btBufferInfoCL( clAabbsWS.getBufferCL(),true),
					btBufferInfoCL( gpuChildShapes.getBufferCL(),true),
					btBufferInfoCL( gpuCompoundPairs.getBufferCL()),
					btBufferInfoCL( numCompoundPairsOut.getBufferCL())
				};

				btLauncherCL launcher(m_queue, m_findCompoundPairsKernel);
				launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
				launcher.setConst( nPairs  );
				launcher.setConst( compoundPairCapacity);

				int num = nPairs;
				launcher.launch1D( num);
				clFinish(m_queue);
			}


			numCompoundPairs = numCompoundPairsOut.at(0);
			//printf("numCompoundPairs =%d\n",numCompoundPairs );
			if (numCompoundPairs > compoundPairCapacity)
				numCompoundPairs = compoundPairCapacity;

			gpuCompoundPairs.resize(numCompoundPairs);
			gpuHasCompoundSepNormals.resize(numCompoundPairs);
			gpuCompoundSepNormals.resize(numCompoundPairs);
			

			if (numCompoundPairs)
			{
#ifndef CHECK_ON_HOST
				BT_PROFILE("processCompoundPairsPrimitivesKernel");
				btBufferInfoCL bInfo[] = 
				{ 
					btBufferInfoCL( gpuCompoundPairs.getBufferCL(), true ), 
					btBufferInfoCL( bodyBuf->getBufferCL(),true), 
					btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
					btBufferInfoCL( convexData.getBufferCL(),true),
					btBufferInfoCL( gpuVertices.getBufferCL(),true),
					btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
					btBufferInfoCL( gpuFaces.getBufferCL(),true),
					btBufferInfoCL( gpuIndices.getBufferCL(),true),
					btBufferInfoCL( clAabbsWS.getBufferCL(),true),
					btBufferInfoCL( gpuChildShapes.getBufferCL(),true),
					btBufferInfoCL( contactOut->getBufferCL()),
					btBufferInfoCL( m_totalContactsOut.getBufferCL())	
				};

				btLauncherCL launcher(m_queue, m_processCompoundPairsPrimitivesKernel);
				launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
				launcher.setConst( numCompoundPairs  );
				launcher.setConst(maxContactCapacity);

				int num = numCompoundPairs;
				launcher.launch1D( num);
				clFinish(m_queue);
				nContacts = m_totalContactsOut.at(0);
#endif
			}
			

			if (numCompoundPairs)
			{

				BT_PROFILE("processCompoundPairsKernel");
				btBufferInfoCL bInfo[] = 
				{ 
					btBufferInfoCL( gpuCompoundPairs.getBufferCL(), true ), 
					btBufferInfoCL( bodyBuf->getBufferCL(),true), 
					btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
					btBufferInfoCL( convexData.getBufferCL(),true),
					btBufferInfoCL( gpuVertices.getBufferCL(),true),
					btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
					btBufferInfoCL( gpuFaces.getBufferCL(),true),
					btBufferInfoCL( gpuIndices.getBufferCL(),true),
					btBufferInfoCL( clAabbsWS.getBufferCL(),true),
					btBufferInfoCL( gpuChildShapes.getBufferCL(),true),
					btBufferInfoCL( gpuCompoundSepNormals.getBufferCL()),
					btBufferInfoCL( gpuHasCompoundSepNormals.getBufferCL())
				};

				btLauncherCL launcher(m_queue, m_processCompoundPairsKernel);
				launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
				launcher.setConst( numCompoundPairs  );

				int num = numCompoundPairs;
				launcher.launch1D( num);
				clFinish(m_queue);
			
			}


			//printf("numConcave  = %d\n",numConcave);

		}//if (findSeparatingAxisOnGpu)


//		printf("hostNormals.size()=%d\n",hostNormals.size());
		//int numPairs = pairCount.at(0);
		
		
		
	}
#ifdef __APPLE__
	bool contactClippingOnGpu = true;
#else
 bool contactClippingOnGpu = true;
#endif
	
	if (contactClippingOnGpu)
	{
		//BT_PROFILE("clipHullHullKernel");

		
		m_totalContactsOut.copyFromHostPointer(&nContacts,1,0,true);

		//concave-convex contact clipping

		if (numConcavePairs)
		{
			BT_PROFILE("clipHullHullConcaveConvexKernel");
			nContacts = m_totalContactsOut.at(0);
			btBufferInfoCL bInfo[] = { 
				btBufferInfoCL( triangleConvexPairsOut.getBufferCL(), true ), 
				btBufferInfoCL( bodyBuf->getBufferCL(),true), 
				btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
				btBufferInfoCL( convexData.getBufferCL(),true),
				btBufferInfoCL( gpuVertices.getBufferCL(),true),
				btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
				btBufferInfoCL( gpuFaces.getBufferCL(),true),
				btBufferInfoCL( gpuIndices.getBufferCL(),true),
				btBufferInfoCL( concaveSepNormals.getBufferCL()),
				btBufferInfoCL( contactOut->getBufferCL()),
				btBufferInfoCL( m_totalContactsOut.getBufferCL())	
			};
			btLauncherCL launcher(m_queue, m_clipHullHullConcaveConvexKernel);
			launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst( numConcavePairs  );
			int num = numConcavePairs;
			launcher.launch1D( num);
			clFinish(m_queue);
			nContacts = m_totalContactsOut.at(0);
		}


		//convex-convex contact clipping
        if (1)
		{
			BT_PROFILE("clipHullHullKernel");
			bool breakupKernel = false;

#ifdef __APPLE__
			breakupKernel = true;
#endif

			if (breakupKernel)
			{


			
            int vertexFaceCapacity = 64;
            
            
            worldVertsB1GPU.resize(vertexFaceCapacity*nPairs);
            
            
            clippingFacesOutGPU.resize(nPairs);
            
            
            worldNormalsAGPU.resize(nPairs);
            
            
            worldVertsA1GPU.resize(vertexFaceCapacity*nPairs);
            
             
            worldVertsB2GPU.resize(vertexFaceCapacity*nPairs);
        
            
            
            {
				BT_PROFILE("findClippingFacesKernel");
            btBufferInfoCL bInfo[] = {
                btBufferInfoCL( pairs->getBufferCL(), true ),
                btBufferInfoCL( bodyBuf->getBufferCL(),true),
                btBufferInfoCL( gpuCollidables.getBufferCL(),true),
                btBufferInfoCL( convexData.getBufferCL(),true),
                btBufferInfoCL( gpuVertices.getBufferCL(),true),
                btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
                btBufferInfoCL( gpuFaces.getBufferCL(),true), 
                btBufferInfoCL( gpuIndices.getBufferCL(),true),
                btBufferInfoCL( sepNormals.getBufferCL()),
                btBufferInfoCL( hasSeparatingNormals.getBufferCL()),
                btBufferInfoCL( clippingFacesOutGPU.getBufferCL()),
                btBufferInfoCL( worldVertsA1GPU.getBufferCL()),
                btBufferInfoCL( worldNormalsAGPU.getBufferCL()),
                btBufferInfoCL( worldVertsB1GPU.getBufferCL())
            };
            
            btLauncherCL launcher(m_queue, m_findClippingFacesKernel);
            launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
            launcher.setConst( vertexFaceCapacity);
            launcher.setConst( nPairs  );
            int num = nPairs;
            launcher.launch1D( num);
            clFinish(m_queue);

            }
            
  
          
            

            ///clip face B against face A, reduce contacts and append them to a global contact array
            if (1)
            {
				BT_PROFILE("clipFacesAndContactReductionKernel");
				//nContacts = m_totalContactsOut.at(0);
				//int h = hasSeparatingNormals.at(0);
				//int4 p = clippingFacesOutGPU.at(0);
                btBufferInfoCL bInfo[] = {
                    btBufferInfoCL( pairs->getBufferCL(), true ),
                    btBufferInfoCL( bodyBuf->getBufferCL(),true),
                    btBufferInfoCL( sepNormals.getBufferCL()),
                    btBufferInfoCL( hasSeparatingNormals.getBufferCL()),
					btBufferInfoCL( contactOut->getBufferCL()),
                    btBufferInfoCL( clippingFacesOutGPU.getBufferCL()),
                    btBufferInfoCL( worldVertsA1GPU.getBufferCL()),
                    btBufferInfoCL( worldNormalsAGPU.getBufferCL()),
                    btBufferInfoCL( worldVertsB1GPU.getBufferCL()),
                    btBufferInfoCL( worldVertsB2GPU.getBufferCL()),
					btBufferInfoCL( m_totalContactsOut.getBufferCL())
                };
                
                btLauncherCL launcher(m_queue, m_clipFacesAndContactReductionKernel);
                launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
                launcher.setConst(vertexFaceCapacity);

				launcher.setConst( nPairs  );
                int debugMode = 0;
				launcher.setConst( debugMode);

				/*
				int serializationBytes = launcher.getSerializationBufferSize();
				unsigned char* buf = (unsigned char*)malloc(serializationBytes+1);
				int actualWritten = launcher.serializeArguments(buf,serializationBytes+1);
				FILE* f = fopen("clipFacesAndContactReductionKernel.bin","wb");
				fwrite(buf,actualWritten,1,f);
				fclose(f);
				free(buf);
				printf("serializationBytes=%d, actualWritten=%d\n",serializationBytes,actualWritten);
				*/

                int num = nPairs;

                launcher.launch1D( num);
                clFinish(m_queue);
                {
//                    nContacts = m_totalContactsOut.at(0);
  //                  printf("nContacts = %d\n",nContacts);
                    
                    contactOut->reserve(nContacts+nPairs);
                    
                    {
                        BT_PROFILE("newContactReductionKernel");
                            btBufferInfoCL bInfo[] =
                        {
                            btBufferInfoCL( pairs->getBufferCL(), true ),
                            btBufferInfoCL( bodyBuf->getBufferCL(),true),
                            btBufferInfoCL( sepNormals.getBufferCL()),
                            btBufferInfoCL( hasSeparatingNormals.getBufferCL()),
                            btBufferInfoCL( contactOut->getBufferCL()),
                            btBufferInfoCL( clippingFacesOutGPU.getBufferCL()),
                            btBufferInfoCL( worldVertsB2GPU.getBufferCL()),
                            btBufferInfoCL( m_totalContactsOut.getBufferCL())
                        };
                        
                        btLauncherCL launcher(m_queue, m_newContactReductionKernel);
                        launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
                        launcher.setConst(vertexFaceCapacity);
                        launcher.setConst( nPairs  );
                        int num = nPairs;
                        
                        launcher.launch1D( num);
                    }
                    nContacts = m_totalContactsOut.at(0);
                    contactOut->resize(nContacts);
                    
//                    Contact4 pt = contactOut->at(0);
                    
  //                  printf("nContacts = %d\n",nContacts);
                }
            }
	}            
	else
	{
	 
		if (nPairs)
		{
			btBufferInfoCL bInfo[] = {
				btBufferInfoCL( pairs->getBufferCL(), true ), 
				btBufferInfoCL( bodyBuf->getBufferCL(),true), 
				btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
				btBufferInfoCL( convexData.getBufferCL(),true),
				btBufferInfoCL( gpuVertices.getBufferCL(),true),
				btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
				btBufferInfoCL( gpuFaces.getBufferCL(),true),
				btBufferInfoCL( gpuIndices.getBufferCL(),true),
				btBufferInfoCL( sepNormals.getBufferCL()),
				btBufferInfoCL( hasSeparatingNormals.getBufferCL()),
				btBufferInfoCL( contactOut->getBufferCL()),
				btBufferInfoCL( m_totalContactsOut.getBufferCL())	
			};
			btLauncherCL launcher(m_queue, m_clipHullHullKernel);
			launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst( nPairs  );
			int num = nPairs;
			launcher.launch1D( num);
			clFinish(m_queue);
		
			nContacts = m_totalContactsOut.at(0);
			contactOut->resize(nContacts);
		}

		int nCompoundsPairs = gpuCompoundPairs.size();

		if (nCompoundsPairs)
		{
				btBufferInfoCL bInfo[] = {
				btBufferInfoCL( gpuCompoundPairs.getBufferCL(), true ), 
				btBufferInfoCL( bodyBuf->getBufferCL(),true), 
				btBufferInfoCL( gpuCollidables.getBufferCL(),true), 
				btBufferInfoCL( convexData.getBufferCL(),true),
				btBufferInfoCL( gpuVertices.getBufferCL(),true),
				btBufferInfoCL( gpuUniqueEdges.getBufferCL(),true),
				btBufferInfoCL( gpuFaces.getBufferCL(),true),
				btBufferInfoCL( gpuIndices.getBufferCL(),true),
				btBufferInfoCL( gpuChildShapes.getBufferCL(),true),
				btBufferInfoCL( gpuCompoundSepNormals.getBufferCL(),true),
				btBufferInfoCL( gpuHasCompoundSepNormals.getBufferCL(),true),
				btBufferInfoCL( contactOut->getBufferCL()),
				btBufferInfoCL( m_totalContactsOut.getBufferCL())	
			};
			btLauncherCL launcher(m_queue, m_clipCompoundsHullHullKernel);
			launcher.setBuffers( bInfo, sizeof(bInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst( nCompoundsPairs  );
			int num = nCompoundsPairs;
			launcher.launch1D( num);
			clFinish(m_queue);
		
			nContacts = m_totalContactsOut.at(0);
			contactOut->resize(nContacts);
		}
		}
		}

	}
}
