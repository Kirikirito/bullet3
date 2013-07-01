
//#include "GpuDemo.h"

#ifdef _WIN32
#include <Windows.h> //for GetLocalTime/GetSystemTime
#else
#include <sys/time.h>//gettimeofday
#endif

#ifdef __APPLE__
#include "OpenGLWindow/MacOpenGLWindow.h"
#elif defined _WIN32
#include "OpenGLWindow/Win32OpenGLWindow.h"
#elif defined __linux
#include "OpenGLWindow/X11OpenGLWindow.h"
#endif

#include "Bullet3OpenCL/Initialize/b3OpenCLUtils.h"
#include "GpuDemoInternalData.h"

#include "OpenGLWindow/GLPrimitiveRenderer.h"
#include "OpenGLWindow/GLInstancingRenderer.h"
//#include "OpenGL3CoreRenderer.h"
//#include "b3GpuDynamicsWorld.h"
#include <assert.h>
#include <string.h>
#include "OpenGLTrueTypeFont/fontstash.h"
#include "OpenGLTrueTypeFont/opengl_fontstashcallbacks.h"
#include "gwenUserInterface.h"
#include "ParticleDemo.h"
#include "broadphase/PairBench.h"
#include "rigidbody/GpuRigidBodyDemo.h"
#include "rigidbody/ConcaveScene.h"
#include "rigidbody/GpuConvexScene.h"
#include "rigidbody/GpuCompoundScene.h"
#include "rigidbody/GpuSphereScene.h"
#include "rigidbody/Bullet2FileDemo.h"
#include "softbody/GpuSoftBodyDemo.h"
#include "../btgui/Timing/b3Quickprof.h"

#include "../btgui/OpenGLWindow/GLRenderToTexture.h"
#include "raytrace/RaytracedShadowDemo.h"
#include "shadows/ShadowMapDemo.h"


bool exportFrame=false;
int frameIndex = 0;
GLRenderToTexture* renderTexture =0;
//#include "BroadphaseBenchmark.h"

int g_OpenGLWidth=1024;
int g_OpenGLHeight = 768;
bool dump_timings = false;
extern char OpenSansData[];

static void MyResizeCallback( float width, float height)
{
	g_OpenGLWidth = width;
	g_OpenGLHeight = height;
}

b3gWindowInterface* window=0;
GwenUserInterface* gui  = 0;
bool gPause = false;
bool gReset = false;

enum
{
	MYPAUSE=1,
	MYPROFILE=2,
	MYRESET,
};

enum
{
	MYCOMBOBOX1 = 1,
};

b3AlignedObjectArray<const char*> demoNames;
int selectedDemo = 0;
GpuDemo::CreateFunc* allDemos[]=
{
//		ConcaveCompound2Scene::MyCreateFunc,
//		GpuConvexScene::MyCreateFunc,

	//ConcaveSphereScene::MyCreateFunc,
	


//	ConcaveSphereScene::MyCreateFunc,



	GpuBoxPlaneScene::MyCreateFunc,
	GpuConvexPlaneScene::MyCreateFunc,


	GpuCompoundScene::MyCreateFunc,



	ConcaveSphereScene::MyCreateFunc,

	ConcaveScene::MyCreateFunc,




	ConcaveCompoundScene::MyCreateFunc,

	GpuCompoundPlaneScene::MyCreateFunc,

	GpuSphereScene::MyCreateFunc,

	GpuSoftClothDemo::MyCreateFunc,

	Bullet2FileDemo::MyCreateFunc,

	PairBench::MyCreateFunc,

	GpuRaytraceScene::MyCreateFunc,

	ShadowMapDemo::MyCreateFunc,

	//GpuRigidBodyDemo::MyCreateFunc,

	//BroadphaseBenchmark::CreateFunc,
	//GpuBoxDemo::CreateFunc,



	//ParticleDemo::MyCreateFunc,



	//GpuCompoundDemo::CreateFunc,
	//EmptyDemo::CreateFunc,
};


void	MyComboBoxCallback(int comboId, const char* item)
{
	int numDemos = demoNames.size();
	for (int i=0;i<numDemos;i++)
	{
		if (!strcmp(demoNames[i],item))
		{
			if (selectedDemo != i)
			{
				gReset = true;
				selectedDemo = i;
				printf("selected demo %s!\n", item);
			}
		}
	}


}
void	MyButtonCallback(int buttonId, int state)
{
	switch (buttonId)
	{
	case MYPAUSE:
		{
		gPause =!gPause;
		break;
		}
	case MYPROFILE:
		{
		dump_timings = !dump_timings;
		break;
		}
	case MYRESET:
		{
		gReset=!gReset;
		break;
		}
	default:
		{
			printf("hello\n");
		}
	}
}

static void MyMouseMoveCallback( float x, float y)
{
	if (gui)
	{
		bool handled = gui ->mouseMoveCallback(x,y);
		if (!handled)
			b3DefaultMouseMoveCallback(x,y);
	}
}
static void MyMouseButtonCallback(int button, int state, float x, float y)
{
	if (gui)
	{
		bool handled = gui->mouseButtonCallback(button,state,x,y);
		if (!handled)
			b3DefaultMouseButtonCallback(button,state,x,y);
	}
}


void MyKeyboardCallback(int key, int state)
{
	if (key==B3G_ESCAPE && window)
	{
		window->setRequestExit();
	}
	if (key==B3G_F1)
	{
		exportFrame = true;
	}
	b3DefaultKeyboardCallback(key,state);
}



bool enableExperimentalCpuConcaveCollision=false;




	int droidRegular=0;//, droidItalic, droidBold, droidJapanese, dejavu;

sth_stash* stash=0;
OpenGL2RenderCallbacks* renderCallbacks  = 0;

void exitFont()
{
	sth_delete(stash);
	stash=0;

	delete renderCallbacks;
	renderCallbacks=0;
}
sth_stash* initFont(GLPrimitiveRenderer* primRender)
{
	GLint err;

		struct sth_stash* stash = 0;
	int datasize;

	float sx,sy,dx,dy,lh;
	GLuint texture;

	renderCallbacks = new OpenGL2RenderCallbacks(primRender);

	stash = sth_create(512,512,renderCallbacks);//256,256);//,1024);//512,512);
    err = glGetError();
    assert(err==GL_NO_ERROR);

	if (!stash)
	{
		fprintf(stderr, "Could not create stash.\n");
		return 0;
	}
#ifdef LOAD_FONT_FROM_FILE
	unsigned char* data=0;
	const char* fontPaths[]={
	"./",
	"../../bin/",
	"../bin/",
	"bin/"
	};

	int numPaths=sizeof(fontPaths)/sizeof(char*);

	// Load the first truetype font from memory (just because we can).

	FILE* fp = 0;
	const char* fontPath ="./";
	char fullFontFileName[1024];

	for (int i=0;i<numPaths;i++)
	{

		fontPath = fontPaths[i];
		//sprintf(fullFontFileName,"%s%s",fontPath,"OpenSans.ttf");//"DroidSerif-Regular.ttf");
		sprintf(fullFontFileName,"%s%s",fontPath,"DroidSerif-Regular.ttf");//OpenSans.ttf");//"DroidSerif-Regular.ttf");
		fp = fopen(fullFontFileName, "rb");
		if (fp)
			break;
	}

    err = glGetError();
    assert(err==GL_NO_ERROR);

    assert(fp);
    if (fp)
    {
        fseek(fp, 0, SEEK_END);
        datasize = (int)ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = (unsigned char*)malloc(datasize);
        if (data == NULL)
        {
            assert(0);
            return 0;
        }
        else
            fread(data, 1, datasize, fp);
        fclose(fp);
        fp = 0;
    }
	if (!(droidRegular = sth_add_font_from_memory(stash, data)))
    {
        assert(0);
        return 0;
    }
    err = glGetError();
    assert(err==GL_NO_ERROR);

	// Load the remaining truetype fonts directly.
    sprintf(fullFontFileName,"%s%s",fontPath,"DroidSerif-Italic.ttf");

	if (!(droidItalic = sth_add_font(stash,fullFontFileName)))
	{
        assert(0);
        return 0;
    }
     sprintf(fullFontFileName,"%s%s",fontPath,"DroidSerif-Bold.ttf");

	if (!(droidBold = sth_add_font(stash,fullFontFileName)))
	{
        assert(0);
        return 0;
    }
    err = glGetError();
    assert(err==GL_NO_ERROR);

     sprintf(fullFontFileName,"%s%s",fontPath,"DroidSansJapanese.ttf");
    if (!(droidJapanese = sth_add_font(stash,fullFontFileName)))
	{
        assert(0);
        return 0;
    }
#else//LOAD_FONT_FROM_FILE
	char* data2 = OpenSansData;
	unsigned char* data = (unsigned char*) data2;
	if (!(droidRegular = sth_add_font_from_memory(stash, data)))
	{
		printf("error!\n");
	}

#endif//LOAD_FONT_FROM_FILE
    err = glGetError();
    assert(err==GL_NO_ERROR);

	return stash;
}




#include "OpenGLWindow/OpenGLInclude.h"
#include "Bullet3Common/b3CommandLineArgs.h"

void Usage()
{
	printf("\nprogram.exe [--selected_demo=<int>] [--cl_device=<int>] [--benchmark] [--dump_timings] [--disable_opencl] [--cl_platform=<int>]  [--x_dim=<int>] [--y_dim=<num>] [--z_dim=<int>] [--x_gap=<float>] [--y_gap=<float>] [--z_gap=<float>] [--use_concave_mesh] [--new_batching] [--no_instanced_collision_shapes]\n");
};


void	DumpSimulationTime(FILE* f)
{
	b3ProfileIterator* profileIterator = b3ProfileManager::Get_Iterator();

	profileIterator->First();
	if (profileIterator->Is_Done())
		return;

	float accumulated_time=0,parent_time = profileIterator->Is_Root() ? b3ProfileManager::Get_Time_Since_Reset() : profileIterator->Get_Current_Parent_Total_Time();
	int i;
	int frames_since_reset = b3ProfileManager::Get_Frame_Count_Since_Reset();

	//fprintf(f,"%.3f,",	parent_time );
	float totalTime = 0.f;



	static bool headersOnce = true;

	if (headersOnce)
	{
		headersOnce = false;
		fprintf(f,"root,");

			for (i = 0; !profileIterator->Is_Done(); i++,profileIterator->Next())
		{
			float current_total_time = profileIterator->Get_Current_Total_Time();
			accumulated_time += current_total_time;
			float fraction = parent_time > B3_EPSILON ? (current_total_time / parent_time) * 100 : 0.f;
			const char* name = profileIterator->Get_Current_Name();
			fprintf(f,"%s,",name);
		}
		fprintf(f,"\n");
	}


	fprintf(f,"%.3f,",parent_time);
	profileIterator->First();
	for (i = 0; !profileIterator->Is_Done(); i++,profileIterator->Next())
	{
		float current_total_time = profileIterator->Get_Current_Total_Time();
		accumulated_time += current_total_time;
		float fraction = parent_time > B3_EPSILON ? (current_total_time / parent_time) * 100 : 0.f;
		const char* name = profileIterator->Get_Current_Name();
		//if (!strcmp(name,"stepSimulation"))
		{
			fprintf(f,"%.3f,",current_total_time);
		}
		totalTime += current_total_time;
		//recurse into children
	}

	fprintf(f,"\n");


	b3ProfileManager::Release_Iterator(profileIterator);


}
///extern const char* g_deviceName;
const char* g_deviceName = "blaat";
extern bool useNewBatchingKernel;
#include "Bullet3Common/b3Vector3.h"

FILE* defaultOutput = stdout;

void myprintf(const char* msg)
{
	fprintf(defaultOutput,msg);
}





//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "OpenGLTrueTypeFont/stb_image_write.h"
void writeTextureToPng(int textureWidth, int textureHeight, const char* fileName)
{
	int numComponents = 4;
	//glPixelStorei(GL_PACK_ALIGNMENT,1);
	GLuint err=glGetError();
	assert(err==GL_NO_ERROR);
	glReadBuffer(GL_BACK);//COLOR_ATTACHMENT0);
	err=glGetError();
	assert(err==GL_NO_ERROR);
	float* orgPixels = (float*)malloc(textureWidth*textureHeight*numComponents*4);
	glReadPixels(0,0,textureWidth, textureHeight, GL_RGBA, GL_FLOAT, orgPixels);
	//it is useful to have the actual float values for debugging purposes

	//convert float->char
	char* pixels = (char*)malloc(textureWidth*textureHeight*numComponents);
	err=glGetError();
	assert(err==GL_NO_ERROR);
		
	for (int j=0;j<textureHeight;j++)
	{
		for (int i=0;i<textureWidth;i++)
		{
			pixels[(j*textureWidth+i)*numComponents] = orgPixels[(j*textureWidth+i)*numComponents]*255.f;
			pixels[(j*textureWidth+i)*numComponents+1]=orgPixels[(j*textureWidth+i)*numComponents+1]*255.f;
			pixels[(j*textureWidth+i)*numComponents+2]=orgPixels[(j*textureWidth+i)*numComponents+2]*255.f;
			pixels[(j*textureWidth+i)*numComponents+3]=orgPixels[(j*textureWidth+i)*numComponents+3]*255.f;
		}
	}

	if (1)
	{
		//swap the pixels
		unsigned char tmp;
		
		for (int j=0;j<textureHeight/2;j++)
		{
			for (int i=0;i<textureWidth;i++)
			{
				for (int c=0;c<numComponents;c++)
				{
					tmp = pixels[(j*textureWidth+i)*numComponents+c];
					pixels[(j*textureWidth+i)*numComponents+c]=
					pixels[((textureHeight-j-1)*textureWidth+i)*numComponents+c];
					pixels[((textureHeight-j-1)*textureWidth+i)*numComponents+c] = tmp;
				}
			}
		}
	}
	
	stbi_write_png(fileName, textureWidth,textureHeight, numComponents, pixels, textureWidth*numComponents);
	
	free(pixels);
	free(orgPixels);

}

int main(int argc, char* argv[])
{
	//b3OpenCLUtils::setCachePath("/Users/erwincoumans/develop/mycache");
	
	b3SetCustomEnterProfileZoneFunc(b3ProfileManager::Start_Profile);
	b3SetCustomLeaveProfileZoneFunc(b3ProfileManager::Stop_Profile);


	b3SetCustomPrintfFunc(myprintf);
	b3Vector3 test(1,2,3);
	test.x = 1;
	test.y = 4;

    printf("main start");

	b3CommandLineArgs args(argc,argv);
	ParticleDemo::ConstructionInfo ci;

	if (args.CheckCmdLineFlag("help"))
	{
		Usage();
		return 0;
	}


	args.GetCmdLineArgument("selected_demo",selectedDemo);


	if (args.CheckCmdLineFlag("new_batching"))
	{
		useNewBatchingKernel = true;
	}
	bool benchmark=args.CheckCmdLineFlag("benchmark");
	dump_timings=args.CheckCmdLineFlag("dump_timings");
	ci.useOpenCL = !args.CheckCmdLineFlag("disable_opencl");
	ci.m_useConcaveMesh = true;//args.CheckCmdLineFlag("use_concave_mesh");
	if (ci.m_useConcaveMesh)
	{
		enableExperimentalCpuConcaveCollision = true;
	}
	ci.m_useInstancedCollisionShapes = !args.CheckCmdLineFlag("no_instanced_collision_shapes");
	args.GetCmdLineArgument("cl_device", ci.preferredOpenCLDeviceIndex);
	args.GetCmdLineArgument("cl_platform", ci.preferredOpenCLPlatformIndex);
	args.GetCmdLineArgument("x_dim", ci.arraySizeX);
	args.GetCmdLineArgument("y_dim", ci.arraySizeY);
	args.GetCmdLineArgument("z_dim", ci.arraySizeZ);
	args.GetCmdLineArgument("x_gap", ci.gapX);
	args.GetCmdLineArgument("y_gap", ci.gapY);
	args.GetCmdLineArgument("z_gap", ci.gapZ);



#ifndef B3_NO_PROFILE
	b3ProfileManager::Reset();
#endif //B3_NO_PROFILE


	window = new b3gDefaultOpenGLWindow();

	b3gWindowConstructionInfo wci(g_OpenGLWidth,g_OpenGLHeight);

	window->createWindow(wci);
	window->setResizeCallback(MyResizeCallback);
	window->setMouseMoveCallback(MyMouseMoveCallback);
	window->setMouseButtonCallback(MyMouseButtonCallback);
	window->setKeyboardCallback(MyKeyboardCallback);

	window->setWindowTitle("Bullet 3.x GPU Rigid Body http://bulletphysics.org");
	printf("-----------------------------------------------------\n");


#ifndef __APPLE__
	glewInit();
#endif

	gui = new GwenUserInterface();

    printf("started GwenUserInterface");


	GLPrimitiveRenderer prim(g_OpenGLWidth,g_OpenGLHeight);

	stash = initFont(&prim);


	if (gui)
	{
		gui->init(g_OpenGLWidth,g_OpenGLHeight,stash,window->getRetinaScale());

		printf("init fonts");


		gui->setToggleButtonCallback(MyButtonCallback);

		gui->registerToggleButton(MYPAUSE,"Pause");
		gui->registerToggleButton(MYPROFILE,"Profile");
		gui->registerToggleButton(MYRESET,"Reset");

		int numItems = sizeof(allDemos)/sizeof(ParticleDemo::CreateFunc*);
		demoNames.clear();
		for (int i=0;i<numItems;i++)
		{
			GpuDemo* demo = allDemos[i]();
			demoNames.push_back(demo->getName());
			delete demo;
		}

		gui->registerComboBox(MYCOMBOBOX1,numItems,&demoNames[0]);
		gui->setComboBoxCallback(MyComboBoxCallback);
	}



	do
	{
		bool syncOnly = false;
		gReset = false;




	static bool once=true;


	glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	window->setWheelCallback(b3DefaultWheelCallback);




	{
		GpuDemo* demo = allDemos[selectedDemo]();
//		demo->myinit();
		bool useGpu = false;

		
		int maxObjectCapacity=128*1024;
		maxObjectCapacity = b3Max(maxObjectCapacity,ci.arraySizeX*ci.arraySizeX*ci.arraySizeX+10);

		{
		ci.m_instancingRenderer = new GLInstancingRenderer(maxObjectCapacity);//render.getInstancingRenderer();
		ci.m_window = window;
		ci.m_gui = gui;
		ci.m_instancingRenderer->init();
		ci.m_instancingRenderer->resize(g_OpenGLWidth,g_OpenGLHeight);
		ci.m_instancingRenderer->InitShaders();
		ci.m_primRenderer = &prim;
//		render.init();
		}

		{
			demo->initPhysics(ci);
		}



		

		printf("-----------------------------------------------------\n");

		FILE* csvFile = 0;
		FILE* detailsFile = 0;

		if (benchmark)
		{
			gPause = false;
			char prefixFileName[1024];
			char csvFileName[1024];
			char detailsFileName[1024];

			b3OpenCLDeviceInfo info;
			b3OpenCLUtils::getDeviceInfo(demo->getInternalData()->m_clDevice,&info);
			
			//todo: move this time stuff into the Platform/Window class
#ifdef _WIN32
			SYSTEMTIME time;
			GetLocalTime(&time);
			char buf[1024];
			DWORD dwCompNameLen = 1024;
			if (0 != GetComputerName(buf, &dwCompNameLen))
			{
				printf("%s", buf);
			} else
			{
				printf("unknown", buf);
			}

			sprintf(prefixFileName,"%s_%s_%s_%d_%d_%d_date_%d-%d-%d_time_%d-%d-%d",info.m_deviceName,buf,demoNames[selectedDemo],ci.arraySizeX,ci.arraySizeY,ci.arraySizeZ,time.wDay,time.wMonth,time.wYear,time.wHour,time.wMinute,time.wSecond);
			
#else
			timeval now;
			gettimeofday(&now,0);
			
			struct tm* ptm;
			ptm = localtime (&now.tv_sec);
			char buf[1024];
#ifdef __APPLE__
			sprintf(buf,"MacOSX");
#else
			sprintf(buf,"Unix");
#endif
			sprintf(prefixFileName,"%s_%s_%s_%d_%d_%d_date_%d-%d-%d_time_%d-%d-%d",info.m_deviceName,buf,demoNames[selectedDemo],ci.arraySizeX,ci.arraySizeY,ci.arraySizeZ,
					ptm->tm_mday,
					ptm->tm_mon+1,
					ptm->tm_year+1900,
					ptm->tm_hour,
					ptm->tm_min,
					ptm->tm_sec);
			
#endif

			sprintf(csvFileName,"%s.csv",prefixFileName);
			sprintf(detailsFileName,"%s.txt",prefixFileName);
			printf("Open csv file %s and details file %s\n", csvFileName,detailsFileName);

			//GetSystemTime(&time2);

			csvFile=fopen(csvFileName,"w");
			detailsFile = fopen(detailsFileName,"w");
			if (detailsFile)
				defaultOutput = detailsFile;

			//if (f)
			//	fprintf(f,"%s (%dx%dx%d=%d),\n",  g_deviceName,ci.arraySizeX,ci.arraySizeY,ci.arraySizeZ,ci.arraySizeX*ci.arraySizeY*ci.arraySizeZ);
		}

		
		fprintf(defaultOutput,"Demo settings:\n");
		fprintf(defaultOutput,"  SelectedDemo=%d, demoname = %s\n", selectedDemo, demo->getName());
		fprintf(defaultOutput,"  x_dim=%d, y_dim=%d, z_dim=%d\n",ci.arraySizeX,ci.arraySizeY,ci.arraySizeZ);
		fprintf(defaultOutput,"  x_gap=%f, y_gap=%f, z_gap=%f\n",ci.gapX,ci.gapY,ci.gapZ);
		fprintf(defaultOutput,"\nOpenCL settings:\n");
		fprintf(defaultOutput,"  Preferred cl_device index %d\n", ci.preferredOpenCLDeviceIndex);
		fprintf(defaultOutput,"  Preferred cl_platform index%d\n", ci.preferredOpenCLPlatformIndex);
		fprintf(defaultOutput,"\n");

		if (demo->getInternalData()->m_platformId)
		{
			b3OpenCLUtils::printPlatformInfo( demo->getInternalData()->m_platformId);
			fprintf(defaultOutput,"\n");
			b3OpenCLUtils::printDeviceInfo( demo->getInternalData()->m_clDevice);
			fprintf(defaultOutput,"\n");
		}
		do
		{


			GLint err = glGetError();
			assert(err==GL_NO_ERROR);


			if (exportFrame)
			{
				
				if (!renderTexture)
				{
					renderTexture = new GLRenderToTexture();
					GLuint renderTextureId;
					glGenTextures(1, &renderTextureId);

					// "Bind" the newly created texture : all future texture functions will modify this texture
					glBindTexture(GL_TEXTURE_2D, renderTextureId);

					// Give an empty image to OpenGL ( the last "0" )
					//glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, g_OpenGLWidth,g_OpenGLHeight, 0,GL_RGBA, GL_UNSIGNED_BYTE, 0);
					//glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA32F, g_OpenGLWidth,g_OpenGLHeight, 0,GL_RGBA, GL_FLOAT, 0);
					glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA32F, g_OpenGLWidth,g_OpenGLHeight, 0,GL_RGBA, GL_FLOAT, 0);

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

					renderTexture->init(g_OpenGLWidth,g_OpenGLHeight,renderTextureId, false);
				}
				
				bool result = renderTexture->enable();
			} 
			
			err = glGetError();
			assert(err==GL_NO_ERROR);

			b3ProfileManager::Reset();
			b3ProfileManager::Increment_Frame_Counter();

//			render.reshape(g_OpenGLWidth,g_OpenGLHeight);
			ci.m_instancingRenderer->resize(g_OpenGLWidth,g_OpenGLHeight);
			prim.setScreenSize(g_OpenGLWidth,g_OpenGLHeight);

			err = glGetError();
			assert(err==GL_NO_ERROR);

			window->startRendering();

			err = glGetError();
			assert(err==GL_NO_ERROR);

			glClear(GL_COLOR_BUFFER_BIT| GL_DEPTH_BUFFER_BIT);//|GL_STENCIL_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);

			err = glGetError();
			assert(err==GL_NO_ERROR);

			if (!gPause)
			{
				B3_PROFILE("clientMoveAndDisplay");

				demo->clientMoveAndDisplay();
			}
			else
			{

			}

			{
				B3_PROFILE("renderScene");
				demo->renderScene();
			}
			err = glGetError();
			assert(err==GL_NO_ERROR);


			/*if (demo->getDynamicsWorld() && demo->getDynamicsWorld()->getNumCollisionObjects())
			{
				B3_PROFILE("renderPhysicsWorld");
				b3AlignedObjectArray<b3CollisionObject*> arr = demo->getDynamicsWorld()->getCollisionObjectArray();
				b3CollisionObject** colObjArray = &arr[0];

				render.renderPhysicsWorld(demo->getDynamicsWorld()->getNumCollisionObjects(),colObjArray, syncOnly);
				syncOnly = true;

			}
			*/

			
			if (exportFrame)
			{
				
				char fileName[1024];
				sprintf(fileName,"screenShot%d.png",frameIndex++);
				writeTextureToPng(g_OpenGLWidth,g_OpenGLHeight,fileName);
				exportFrame = false;
				renderTexture->disable();
			}


			{
				B3_PROFILE("gui->draw");
				if (gui)
					gui->draw(g_OpenGLWidth,g_OpenGLHeight);
			}
			err = glGetError();
			assert(err==GL_NO_ERROR);


			{
				B3_PROFILE("window->endRendering");
				window->endRendering();
			}

			err = glGetError();
			assert(err==GL_NO_ERROR);

			{
				B3_PROFILE("glFinish");
			}

			

			if (dump_timings)
			{
				b3ProfileManager::dumpAll(stdout);
			}

			if (csvFile)
			{
				static int frameCount=0;

				if (frameCount>0)
				{
					DumpSimulationTime(csvFile);
					if (detailsFile)
					{
							fprintf(detailsFile,"\n==================================\nFrame %d:\n", frameCount);
							b3ProfileManager::dumpAll(detailsFile);
					}
				}

				if (frameCount>=102)
					window->setRequestExit();
				frameCount++;
			}




		} while (!window->requestedExit() && !gReset);


		demo->exitPhysics();
		b3ProfileManager::CleanupMemory();
		delete ci.m_instancingRenderer;

		delete demo;
		if (detailsFile)
		{
			fclose(detailsFile);
			detailsFile=0;
		}
		if (csvFile)
		{
			fclose(csvFile);
			csvFile=0;
		}
	}



	} while (gReset);


	if (gui)
		gui->setComboBoxCallback(0);

	{

	

		delete gui;
		gui=0;

		exitFont();


		window->closeWindow();
		delete window;
		window = 0;

	}

	return 0;
}
