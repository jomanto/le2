#include "lost/SunEngine.h"

#include "lost/Log.h"
#include "lost/Bundle.h"
#include "lost/Bitmap.h"
#include "lost/Texture.h"
#include "lost/Context.h"
#include "lost/BufferLayout.h"
#include "lost/Mesh.h"
#include "lost/HybridIndexBuffer.h"
#include "lost/HybridVertexBuffer.h"
#include "lost/Camera2D.h"
#include "lost/TruetypeFont.h"
#include "lost/TextRender.h"
#include "lost/TextMesh.h"
#include "lost/Material.h"
#include "lost/TextBuffer.h"
#include <cmath>

#include "guiro/UserInterface.h"
#include "lost/ResourceManager.h"

#include "lost/MeshAlgo.h"
#include "lost/EventQueue.h"
#include "lost/Event.h"

#include "lost/FrameBuffer.h"
#include "lost/Quad.h"

namespace lost
{

void SunEngine::updateSpline(const vector<Vec2>& cp, u32 numPoints, MeshPtr& triangles)
{
  uint32_t numVertices = numPoints;
  vector<Vec2> ip; // interpolated points
  vector<Vec2> nv; // tangent vectors
  ip.reserve(numVertices);
  nv.reserve(numVertices);
  
  // a minimum of 4 control points is required for the initial segment.
  // Each consecutive is made up by following point + 3 previous.
  uint32_t numSegments = ((uint32_t)cp.size()-4)+1;
  vector<uint32_t> pn; // number of points per segment
  pn.reserve(numSegments);

  // vertices spread evenly between segments
  // leftovers are attached to last segment
  uint32_t nps = numVertices / numSegments;
  uint32_t pbudget = numVertices;
  for(uint32_t i=0; i<numSegments; ++i)
  {
    if(2*nps > pbudget)
    {
      pn[i] = pbudget; // should only be the last one
    }
    else
    {
      pn[i] = nps;
      pbudget -= nps;
    }
//    DOUT("seg "<<pn[i]);
  }
  
  uint32_t pointOffset = 0;
  for(uint32_t segnum = 0; segnum < numSegments; ++segnum)
  {
    catmullRomSegment(ip, pointOffset, pn[segnum], cp, segnum);
    pointOffset += pn[segnum];
  }

  // fix normal vectors
  ASSERT(numVertices >= 2, "numVertices must be at least 2");
  for(u32 i=0; i<(numVertices-1); ++i)
  {
    Vec2 tv = ip[i+1] - ip[i+0];
    Vec2 tnv;
    tnv.x = -tv.y;
    tnv.y = tv.x;
    normalise(tnv);
    nv[i] = tnv;
  }
  nv[numVertices-1] = nv[numVertices-2];
    
  // adjust triangle mesh, only writes to points
  f32 halfWidth = splineWidth / 2;
  u32 j=0;
  
  f32 falloff = halfWidth / numVertices;
  f32 hw = halfWidth;
  for(u32 i=0; i<numVertices; i+=1)
  {
    Vec2 p = ip[i];
    Vec2 n = nv[i];
    Vec2 halfdir = (n*hw);
    Vec2 leftPoint = p+halfdir;
    Vec2 rightPoint = p-halfdir;
    triangles->set(j, UT_position, leftPoint);
    triangles->set(j+1, UT_position, rightPoint);
    
    triangles->set(j, UT_texcoord0, Vec2(0,0));
    triangles->set(j+1, UT_texcoord0, Vec2(1,0));
//    DOUT("p "<< p << " n "<<n);
//    DOUT("left "<<leftPoint<<" right "<<rightPoint );
    j+=2;
    hw -= falloff;
  }
  
}

void SunEngine::startup()
{
  ResourceBundle mainBundle;
  colorShader = resourceManager->shader("resources/glsl/color");
  textureShader = resourceManager->shader("resources/glsl/texture");
  hblurShader = resourceManager->shader("resources/glsl/hblur");
  vblurShader = resourceManager->shader("resources/glsl/vblur");

  winSize = Vec2(800, 600);
  cam = Camera2D::create(Rect(0,0,winSize.width, winSize.height));
  
  splineWidth = 12;
  
  //////////////////////////////////////////
  /// SPLINE
  
  numInterpolatedPoints = 50;
    
  Color splineBorderColor = Color(.39,.75,0.1,1);
  Color splineColor = Color(.24, .55, .0, 1);
  
  BitmapPtr splineBitmap(new Bitmap(splineWidth+2, 1, GL_RGBA));
  splineBitmap->clear(Color(0,0,0,0));
  for(u32 i=1; i<(splineWidth-1); ++i)
  {
    splineBitmap->pixel(i, 0, splineColor);
  }
  
  f32 borderWith = floorf(splineWidth/3);
  for(u32 i=0; i<borderWith; ++i)
  {
    splineBitmap->pixel(i+1, 0, splineBorderColor);
    splineBitmap->pixel(splineWidth-1-i, 0, splineBorderColor);
  }
    
  splineBitmap->premultiplyAlpha();
  splineTexture.reset(new Texture(splineBitmap));
  splineTexture->filter(GL_LINEAR);
  
  d = 0;
  
  ////////////////////////////////////////////
  ////////////////////////////////////////////
  //////// Framebuffer
  ////////////////////////////////////////////
  ////////////////////////////////////////////

  Vec2 fbsize = winSize;

  fbcam = Camera2D::create(Rect(0,0,fbsize.width,fbsize.height));
  
  fb0 = FrameBuffer::create(fbsize, GL_RGBA);
  fb0->check();
  glBindFramebuffer(GL_FRAMEBUFFER, 0); //  switch to default framebuffer again
  fb0quad = Quad::create(fb0->colorBuffers[0]->texture, false);
  fb0quad->material->shader = vblurShader;
  fb0quad->material->color = whiteColor;
  fb0quad->material->blendPremultiplied();

  fb1 = FrameBuffer::create(fbsize, GL_RGBA);
  fb1->check();
  glBindFramebuffer(GL_FRAMEBUFFER, 0); //  switch to default framebuffer again
  fb1quad = Quad::create(fb1->colorBuffers[0]->texture, false);
  fb1quad->material->shader = vblurShader;
  fb1quad->material->color = whiteColor;
  fb1quad->material->blendPremultiplied();
    
  mainRenderFunc = [this] ()
  {
    //////////////////////
    // framebuffer
   
    // pass 1: original into buffer 0
    fb0->bind();
    glContext->clearColor(Color(0, 0, 0, 0));
    glContext->camera(fbcam);
    glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);

    sceneRenderFunc();

    // pass 2: horizontal blur into buffer 1
    fb1->bind();
    glContext->clearColor(Color(0, 0, 0, 0));
    glContext->camera(fbcam);
    glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);

    fb0quad->material->shader = hblurShader;
    glContext->draw(fb0quad);
    
    // pass 3: vertical blur into buffer 0
    fb0->bind();
    glContext->clearColor(Color(0, 0, 0, 0));
    glContext->camera(fbcam);
    glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);

    fb1quad->material->shader = vblurShader;
    glContext->draw(fb1quad);
    
    //////////////////////
    // screen pass
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // switch to default framebuffer


    glContext->clearColor(Color(.1, .3, .3, 0));
    glContext->camera(cam);
    glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);


//    glContext->draw(triangulatedSpline);
    sceneRenderFunc();

    fb0quad->material->shader = textureShader;
    fb0quad->material->color = Color(1,1,0,1);
    glContext->draw(fb0quad);  
  };
  
  
  ////////////// prepare memory
  numCircles = 5;
  numSplines = 22;
  minRadius = 50;
  maxRadius = 300;
  circleCenter = Vec2(winSize.width/2, winSize.height/2);
  dotSize = 7;
//  ASSERT(numCircles >= 2, "numCircles must be >= 2");
  circlePoints = new Vec2[numCircles*numSplines];
  circleRadius.resize(numCircles);

/*  // prepare debug dot meshes
  u32 numDots = numCircles * numSplines;
  for(u32 i=0; i<numDots; ++i)
  {
    QuadPtr m = Quad::create(Rect(0,0,dotSize, dotSize));
    m->material->color = Color(1,0,0,1);
    m->material->shader = colorShader;
    circleDots.push_back(m);
  }*/
  
  // precalculate radii
  for (u32 i=0; i<numCircles; ++i)
  {
    f32 d = numCircles > 1 ? (((maxRadius - minRadius)/(numCircles-1))*i) : 0;
    circleRadius[i] = minRadius + d;
//    DOUT(circleRadius[i]);
  }
  
  // calculate circle points
  for (u32 ci=0; ci<numCircles; ++ci)
  {
    f32 r = circleRadius[ci];
    for(u32 cp=0; cp<numSplines; ++cp)
    {
      f32 f = ((f32)cp)/(f32(numSplines));
      f *= 2*M_PI;
      if(ci==2)
      {
        f+=.2;
      }
      else if(ci==4)
      {
        f-=.7;
      }
      f32 x = sinf(f)*r;
      f32 y = cosf(f)*r;
      Vec2 p = Vec2(x,y)+circleCenter;
      u32 idx = ci*numSplines + cp;
      circlePoints[idx] = p;
    }
  }
  
  // create spline meshes
  for(u32 i=0; i<numSplines; ++i)
  {
    MeshPtr mesh = newTriangleStrip((numInterpolatedPoints*2)-2);
    mesh->material->blendPremultiplied();
    mesh->material->shader=textureShader;
    mesh->material->textures.push_back(splineTexture);
    splines.push_back(mesh);
  }
  
  // update splines
  vector<Vec2> cps;
  cps.resize(numCircles);
  for(u32 i=0; i<numSplines; ++i)
  {
    for(u32 pi = 0; pi<numCircles; ++pi)
    {
      Vec2 v = circlePoints[pi*numSplines+i];
      cps[pi] = v;
    }
    
    vector<Vec2> dv;
    dv.push_back(cps[0]);
    for(auto v : cps)
    {
      dv.push_back(v);
    }
    dv.push_back(cps.back());
    
    updateSpline(dv, numInterpolatedPoints, splines[i]);
  }
  
/*  // apply circlePoints coordinates to debug meshes
  for (u32 ci=0; ci<numCircles; ++ci)
  {
    for(u32 cp=0; cp<numSplines; ++cp)
    {
      u32 idx = ci*numSplines + cp;
      Vec2 p = circlePoints[idx];
      Vec3 tv(p.x, p.y, 0);
      circleDots[idx]->transform = MatrixTranslation(tv);
    }
  }*/
  
    sceneRenderFunc = [this] () {
    glContext->clearColor(Color(.1, .3, .3, 0));
    glContext->camera(cam);
    glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);

    for(auto mesh : splines)
    {
      glContext->draw(mesh);
    }

  };

}


void SunEngine::update()
{
  EventQueue::Container events = eventQueue->getCurrentQueue();
    
  for(Event* event : events)
  {
    if(event->base.type == ET_WindowResize)
    {
      f32 w = event->windowResizeEvent.width;
      f32 h = event->windowResizeEvent.height;
      DOUT("updating viewport "<<int(w)<<"/"<<int(h));
      cam->viewport(Rect(0,0,w,h));
    }
    else if(event->base.type == ET_MouseMoveEvent)
    {
    }
  }

  d += clock.deltaUpdate;

  mainRenderFunc();

/*  for(u32 i=0; i<numSplines*numCircles; ++i)
  {
    auto mesh = circleDots[i];
    glContext->draw(mesh);
  }*/
}

void SunEngine::shutdown()
{
}

}

