/*
Copyright (c) 2012 Tony Kostanjsek, Timo Boll

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "lost/Context.h"
#include "lost/Camera.h"
#include "lost/ShaderProgram.h"
#include "lost/Mesh.h"
#include "lost/Bitmap.h"
#include "lost/HybridBuffer.h"
#include "lost/UniformBlock.h"
#include "lost/Texture.h"
#include "lost/HostBuffer.h"
#include "lost/Buffer.h"
#include "lost/Application.h"
#include "lost/FrameBuffer.h"
#include <algorithm>

namespace lost
{

bool getBoolParam(GLenum pname)
{
  GLboolean result;
  glGetBooleanv(pname, &result);GLDEBUG;
  return result ? true : false;
}

int getIntParam(GLenum pname)
{
  int result;
  glGetIntegerv(pname, &result);GLDEBUG;
  return result;
}

template<typename T>
T getParam(GLenum pname);

template<> bool getParam(GLenum pname) { return getBoolParam(pname);GLDEBUG; }
template<> int getParam(GLenum pname) { return getIntParam(pname);GLDEBUG; }
template<> Color getParam(GLenum pname)
{
  Color result;
  glGetFloatv(pname, result.fv);GLDEBUG;
  return result;
}
template<> Rect getParam(GLenum pname)
{
  float rect[4];
  glGetFloatv(pname, rect);GLDEBUG;
  return Rect(rect[0], rect[1], rect[2], rect[3]);
}


#define CLIENTSTATE(member, newstate, pname)  \
if(member != newstate) \
{ \
  if(newstate) \
  { \
    glEnableClientState(pname);GLDEBUG; \
  } \
  else \
  { \
    glDisableClientState(pname);GLDEBUG; \
  } \
  member = newstate; \
}

#define SERVERSTATE(member, newstate, pname)  \
if(member != newstate) \
{ \
  if(newstate) \
  { \
    glEnable(pname);GLDEBUG; \
  } \
  else \
  { \
    glDisable(pname);GLDEBUG; \
  } \
  member = newstate; \
}

    Context::Context()
    {
      // this is only true on desktop systems.
      // on systems like the iPhone, where you have to create the default buffer yourself, you
      // have to set the default in the context, after creation
      _defaultFrameBuffer = 0;
      _currentFrameBuffer = _defaultFrameBuffer;

      depthTestEnabled = getParam<bool>(GL_DEPTH_TEST);  
      blendEnabled = getParam<bool>(GL_BLEND);
      currentBlendFuncSource = GL_ZERO; //getParam<int>(GL_BLEND_SRC);
      currentBlendFuncDestination = GL_ZERO; //getParam<int>(GL_BLEND_DST);
      scissorEnabled = getParam<bool>(GL_SCISSOR_TEST);
      texture2DEnabled = getParam<bool>(GL_TEXTURE_2D);
      currentActiveTexture = getParam<int>(GL_ACTIVE_TEXTURE);
      currentClearColor = getParam<Color>(GL_COLOR_CLEAR_VALUE);
      currentScissorRect = getParam<Rect>(GL_SCISSOR_BOX);
      cullEnabled = getParam<bool>(GL_CULL_FACE);
      cullFaceMode = getParam<int>(GL_CULL_FACE_MODE);
      currentShader = NULL;
      
      // reset active textures
      for(uint32_t i=0; i<_maxTextures; ++i)
      {
        activeTextures[i] = 0;
      }
      clearVertexAttributeEnabled();
      clearVertexAttributeRequired();
      
      modelViewStack.push_back(Matrix::identity());
    }
    
    Context::~Context()
    {
    }

    Context* Context::instance()
    {
      // deliberately NOT thread_local since context creation can happen on any thread
      // accessing it through Application singleton was easiest, albeit a bit weird. 
      return Application::instance()->glContext;
    }

    void Context::bindDefaultFramebuffer()
    {
      bindFrameBuffer(_defaultFrameBuffer);
    }

    void Context::defaultFramebuffer(GLuint fbo)
    {
      _defaultFrameBuffer = fbo;
    }
    
    GLuint Context::defaultFramebuffer()
    {
      return _defaultFrameBuffer;
    }

    void Context::bindFrameBuffer(FrameBuffer* fb)
    {
      if(fb)
      {
        bindFrameBuffer(fb->buffer);
      }
      else
      {
        EOUT("tried to bind NULL framebuffer");
      }
    }

    void Context::bindFrameBuffer(GLuint fb)
    {
      glBindFramebuffer(GL_FRAMEBUFFER, fb); GLASSERT;
      _currentFrameBuffer = fb;
    }


    void Context::depthTest(bool enable) { SERVERSTATE(depthTestEnabled, enable, GL_DEPTH_TEST); }
    void Context::blend(bool enable) { SERVERSTATE(blendEnabled, enable, GL_BLEND);}

    void Context::cull(bool enable) { SERVERSTATE(cullEnabled, enable, GL_CULL_FACE);}
    void Context::cullFace(GLenum mode)
    {
      if(mode != cullFaceMode)
      {
        glCullFace(mode);GLDEBUG;
        cullFaceMode = mode;
      }
    }
    
    void Context::blendFunc(GLenum src, GLenum dest)
    {
      if((currentBlendFuncSource != src)
         ||
         (currentBlendFuncDestination != dest)
         )
      {
        glBlendFunc(src, dest);GLDEBUG;
        currentBlendFuncSource = src;
        currentBlendFuncDestination = dest;
      }
    }
    
    void Context::scissor(bool enable) {SERVERSTATE(scissorEnabled, enable, GL_SCISSOR_TEST);}
    
    void Context::scissorRect(const Rect& rect)
    {
      if(currentScissorRect != rect)
      {
        glScissor((GLint)rect.x, (GLint)rect.y, (GLsizei)rect.width, (GLsizei)rect.height);
        currentScissorRect = rect;
      }
    }

    void Context::pushScissorRect(const Rect& v)
    {
      _scissorRectStack.push_back(v);
      scissorRect(v);
      scissor(true);
    }
    
    void Context::pushClippedScissorRect(const Rect& v)
    {
      Rect r = v;
      if(_scissorRectStack.size())
      {
        r.clipTo(_scissorRectStack[_scissorRectStack.size()-1]);
      }
      pushScissorRect(r);
    }
    
    void Context::popScissorRect()
    {
      if(!_scissorRectStack.size()) return; // don't do anything if stack is empty
      
      _scissorRectStack.pop_back();
      uint32_t s = (uint32_t)_scissorRectStack.size();
      if(s > 0)
      {
        scissorRect(_scissorRectStack[s-1]);
        scissor(true);
      }
      else
      {
        scissor(false);
      }
    }
  
    void Context::clearColor(const Color& col)
    {
      if(currentClearColor != col)
      {
        glClearColor(col.r(), col.g(), col.b(), col.a()); GLDEBUG;
        currentClearColor = col;
      }
    }

    void Context::viewport(const Rect& val)
    {
      if(val != currentViewport)
      {
        glViewport((GLint)val.x, (GLint)val.y, (GLsizei)val.width, (GLsizei)val.height);GLDEBUG;
        currentViewport = val;
      }
    }

    // only update if the new cam is either a new one or the same one, but with the dirty flag set. 
    void Context::camera(const CameraPtr& cam)
    {
      if (currentCam != cam || currentCam->needsUpdate)
      {
        viewport(cam->viewport());
        if (currentCam != cam) currentCam = cam;
      }
    }

#pragma mark - Texture -
  
    void Context::clear(GLbitfield flags) { glClear(flags);GLDEBUG; }
    
    void Context::activeTexture(GLenum tex)
    {
      if(currentActiveTexture != tex)
      {
        glActiveTexture(tex);GLDEBUG;
        currentActiveTexture = tex;
      }
    }
    
    void Context::bindTexture(GLuint tex)
    {
      uint32_t idx = currentActiveTexture - GL_TEXTURE0; // have to subtract since GL_TEXTURE0 is some arbitrary hex value and not zero based
      assert((idx>=0) && (idx<_maxTextures));
      if(activeTextures[idx] != tex)
      {
        glBindTexture(GL_TEXTURE_2D, tex);GLASSERT;
        activeTextures[idx] = tex;
      }
    }

    void Context::bindTexture(Texture* texture)
    {
      bindTexture(texture->texture);
    }
  
    void Context::bindTextures(const vector<TexturePtr>& textures)
    {
      if(textures.size() > 0)
      {
        size_t num = textures.size();
        for(size_t i=0; i<num; ++i)
        {
          activeTexture((uint32_t)(GL_TEXTURE0+i)); // the standard guarantees GL_TEXTUREi = GL_TEXTURE0+i
          Texture* texture = textures[i].get();
          bindTexture(texture->texture);
        }
        activeTexture(GL_TEXTURE0); // reset 
      }      
    }

#pragma mark - Shader -

    void Context::enableShader(ShaderProgram* prog)
    {
      if(currentShader && !prog)
      {
        currentShader->disable();
      }
      
      if(currentShader != prog)
      {
        currentShader = prog;
        if(currentShader)
        {
          glUseProgram(currentShader->program);GLASSERT;
        }
      }
    }

    void Context::disableShader()
    {
      glUseProgram(0); // don't check for error here because calling it with 0 always results in an error, which is perfectly ok
      currentShader = NULL;
    }
  
    void Context::material(const MaterialPtr& mat)
    {
      if(mat->textures.size()>0)
      {
        bindTextures(mat->textures);
      }
      if(mat->blend)
      {
        blend(true);
        blendFunc(mat->blendSrc, mat->blendDest);
      }
      else
      {
        blend(false);
      }
      if(mat->cull)
      {
        cull(true);
        cullFace(mat->cullMode);
      }
      else
      {
        cull(false);
      }
      enableShader(mat->shader.get());
    }

    void Context::applyUniforms(UniformBlock* ub)
    {
      for(UniformBlock::VariantMap::iterator i = ub->variantMap.begin(); 
          i != ub->variantMap.end();
          ++i)
      {
        Variant& v = i->second;
        string name = i->first;
        switch(v.type)
        {
          case VT_int     : currentShader->setInt(name, v.i);   break;
          case VT_float   : currentShader->setFloat(name, v.f); break;
          case VT_bool    : currentShader->setBool(name, v.b);  break;
          case VT_color   : currentShader->set(name, v.color);  break;
          case VT_vec2    : currentShader->set(name, v.vec2);   break;
          case VT_vec3    : currentShader->set(name, v.vec3);   break;
          case VT_vec4    : currentShader->set(name, v.vec4);   break;
          case VT_matrix  : currentShader->set(name, v.matrix); break;
          default         : break;
        }
      }
    }

    void Context::vertexAttributeEnable(uint32_t idx, bool enable)
    {
      _vertexAttributeRequired[idx] = enable;
      if(_vertexAttributeEnabled[idx] != enable)
      {
        _vertexAttributeEnabled[idx] = enable;
        if(enable){
          glEnableVertexAttribArray(idx);GLDEBUG;
        }
        else {
          glDisableVertexAttribArray(idx);GLDEBUG;          
        }
      }
    }
    
    void Context::clearVertexAttributeEnabled()
    {
      for(uint32_t i=0; i<_maxVertexAttributes; ++i)
      {
        _vertexAttributeEnabled[i] = false;
      }
    }
    
    void Context::clearVertexAttributeRequired()
    {
      for(uint32_t i=0; i<_maxVertexAttributes; ++i)
      {
        _vertexAttributeRequired[i] = false;
      }
    }

    void Context::disableUnrequiredVertexAttributes()
    {
      for(uint32_t i=0; i<_maxVertexAttributes; ++i)
      {
        if(!_vertexAttributeRequired[i] && _vertexAttributeEnabled[i])
        {
          vertexAttributeEnable(i, false);
        }
      }
    }
        
    void Context::draw(const MeshPtr& mesh)
    {
      clearVertexAttributeRequired();
      HybridIndexBuffer* ib = mesh->indexBuffer.get();
      HybridVertexBuffer* vb = mesh->vertexBuffer.get();

      if(ib->dirty) {ib->upload();}
      if(vb->dirty) {vb->upload();}

      bind(vb->gpuBuffer.get());
      bind(ib->gpuBuffer.get());
      
      if(mesh->material)
      {
        material(mesh->material);      
      }

      // don't do anything if there's no shader
      if(currentShader)
      {      
        // set automatic uniforms if the shader wants them
        if(currentShader->hasUniform("projectionMatrix")) { currentShader->set("projectionMatrix", currentCam->projectionMatrix() * currentCam->viewMatrix()); }
        if(currentShader->hasUniform("modelViewMatrix")) { currentShader->set("modelViewMatrix", mesh->transform * modelViewStack.back()); }
        if(currentShader->hasUniform("viewport")) { Rect v = currentCam->viewport(); currentShader->set("viewport", Vec2(v.width, v.height)); }
        if(currentShader->hasUniform("depth")) { currentShader->set("depth", currentCam->depth()); }
        if(currentShader->hasUniform("color")) { currentShader->set("color", mesh->material->color); }
        if(currentShader->hasUniform("texture0")) { currentShader->setInt("texture0", 0); }
        if(currentShader->hasUniform("texture1")) { currentShader->setInt("texture1", 1); }
        if(currentShader->hasUniform("texture2")) { currentShader->setInt("texture2", 2); }
        
        // set mesh specific uniforms after automatic ones
        if(mesh->material->uniforms)
        {
          applyUniforms(mesh->material->uniforms.get());
        }

        // map vertex attributes from buffer to shader according to the vertex buffers attribute map
        HybridBuffer::VertexAttributeMap::iterator i = vb->vertexAttributeMap.begin();
        HybridBuffer::VertexAttributeMap::iterator end = vb->vertexAttributeMap.end();
        for(i = vb->vertexAttributeMap.begin(); i!= end; ++i)
        {
          const string& attributeName = i->second;
          UsageType ut = i->first;
          if(currentShader->hasAttribute(attributeName))
          {
            const VertexAttribute& va = currentShader->name2vertexAttribute[attributeName];
            const AttributePointerConfig apc = vb->pointerConfigForUsageType(ut);
            vertexAttributeEnable(va.location, true);
            glVertexAttribPointer(va.location, apc.size, apc.type, apc.normalise, apc.stride, apc.offset);GLDEBUG;
          }
        }
      }
      
      disableUnrequiredVertexAttributes();
      glDrawElements(ib->drawMode, ib->hostBuffer->count, ib->type, 0);GLDEBUG;
    }

    /** Uses glReadPixels to retrieve the current framebuffer data as rgba and saves it
     * as a tga file to the specified file path.
     *
     * @param fullPathName full path name of file to be saved. You must ensure that the location is writable.
     *        
     */
    void Context::writeScreenshot(const string& fullPathName,
                                 bool withAlphaChannel)
    {
      GLenum format = withAlphaChannel ? GL_RGBA : GL_RGB;
      GLenum bitmapFormat = withAlphaChannel ? GL_RGBA : GL_RGB;
      Bitmap bmp((uint32_t)currentCam->viewport().width,
                 (uint32_t)currentCam->viewport().height,
                 bitmapFormat);
      glReadPixels(0,
                   0,
                   bmp.width,
                   bmp.height,
                   format,
                   GL_UNSIGNED_BYTE,                        
                   bmp.data); GLASSERT;
      bmp.flip();
      bmp.write(fullPathName);
    } 

    void Context::bind(Buffer* buffer)
    {
      auto pos = target2buffer.find(buffer->target);
      if(pos == target2buffer.end() || (pos->second != buffer->buffer))
      {
        glBindBuffer(buffer->target, buffer->buffer);GLASSERT;
        target2buffer[buffer->target] = buffer->buffer;
      }
      else
      {
      }
    }
  
    void Context::pushModelViewMatrix(const Matrix& matrix)
    {
      Matrix current = matrix * modelViewStack.back();
      modelViewStack.push_back(current);
    }

    void Context::popModelViewMatrix()
    {
      if(modelViewStack.size() > 1)
      {
        modelViewStack.pop_back();
      }
    }

#pragma mark - Resource Lifecycle & Cache Sync -

void Context::logTextureStats()
{
  DOUT("--- Texture Stats:");
  DOUT("alive: "<<(s64)_textures.size());
  f64 numBytes = 0;
  for(Texture* tex : _textures)
  {
    u64 bpp = 0;
    switch(tex->internalFormat)
    {
      case GL_ALPHA:bpp=1;break;
      case GL_RGB:bpp=3;break;
      case GL_RGBA:bpp=4;break;
      default:
        EOUT("dunno bpp:"<<tex->internalFormat);
    }
    f64 sz =tex->width*tex->height*bpp;
    numBytes += sz;
//    DOUT(tex->texture<<" size: "<<sz/(1024*1024)<<" MB");
  }
  DOUT("memory: "<<numBytes/(1024*1024)<<" MB");
}

void Context::textureCreated(Texture* tex)
{
  auto pos = find(_textures.begin(), _textures.end(), tex);
  if(pos == _textures.end())
  {
    _textures.push_back(tex);
//    DOUT("num textures: "<<(s64)_textures.size());
  }
}

void Context::textureDying(Texture* tex)
{
//  DOUT("");
  for(u32 i=0; i<_maxTextures; ++i)
  {
    if(activeTextures[i] == tex->texture)
    {
      activeTextures[i] = 0;
      DOUT("removing dying texture "<<tex->texture);
    }
  }

  auto pos = find(_textures.begin(), _textures.end(), tex);
  if(pos != _textures.end())
  {
//    DOUT("removing global texture referebce: "<<tex->texture);
    _textures.erase(pos);
  }
}

void Context::bufferDying(Buffer* buffer)
{
//  DOUT("");
  for(auto i : target2buffer)
  {
    if(i.second == buffer->buffer)
    {
      i.second = 0;
      DOUT("removing dying Buffer "<<buffer->buffer);
    }
  }
}

void Context::shaderprogramDying(ShaderProgram* prog)
{
  DOUT("");
  if(currentShader == prog)
  {
    disableShader();
    currentShader = NULL;
    DOUT("removing dying shader "<<prog->program);
  }
}

void Context::framebufferDying(FrameBuffer* fb)
{
  DOUT("");
  if(_currentFrameBuffer == fb->buffer)
  {
    DOUT("removing dying framebuffer");
    if(fb->buffer != _defaultFrameBuffer)
    {
      bindDefaultFramebuffer();
    }
  }
}
  
}
