#include "apps/UiTestApp.h"
#include "lost/Context.h"
#include "guiro/UserInterface.h"
#include "guiro/layers/TextLayer.h"
#include "lost/ResourceManager.h"

namespace lost
{

void UiTestApp::startup()
{
  ui->enable();

  ui->rootView->layer->backgroundColor = clearColor;
  ui->rootView->layer->name = "root";
  
  LayerPtr sl1(new Layer);
  sl1->rect(Rect(50,50,50,50));
  sl1->backgroundColor = greenColor;
  sl1->name = "green";
  sl1->cornerRadius = 4;
  
  LayerPtr sl2(new Layer);
  sl2->rect(Rect(75,100,30,40));
  sl2->backgroundColor = yellowColor;
  sl2->name = "yellow";
  sl2->cornerRadius = 12;

  LayerPtr sl3(new Layer);
  sl3->rect(Rect(0,0,90,10));
  sl3->backgroundColor = blueColor;
  sl3->name = "blue";
  sl3->cornerRadius = 4;

  LayerPtr sl4(new Layer);
  sl4->rect(Rect(100,50,50,50));
  sl4->backgroundColor = whiteColor;
  sl4->name = "reddy";
  sl4->cornerRadius = 20;
  sl4->borderColor = blueColor;
  sl4->borderWidth = 1;

  
  ui->rootView->layer->addSublayer(sl1);
  ui->rootView->layer->addSublayer(sl2);
  sl2->addSublayer(sl3);
  ui->rootView->layer->addSublayer(sl4);
  
    

  DOUT("root Z:"<<ui->rootView->layer->z(););
  DOUT("sl1 Z:"<<sl1->z());
  DOUT("sl2 Z:"<<sl2->z());
  DOUT("sl3 Z:"<<sl3->z());

  #define SZDOUT(c) DOUT("sizeof("<<#c<<") = "<<u64(sizeof(c)));
  SZDOUT(Layer);
  SZDOUT(LayerPtr);
  SZDOUT(string);
  SZDOUT(vector<LayerPtr>);
  SZDOUT(View);
  SZDOUT(Color);
  SZDOUT(Frame);
  
  resourceManager->registerFontBundle("resources/fonts/vera.lefont");
  FontPtr font = resourceManager->font("Vera", 12);

  {
    TextLayerPtr tl(new TextLayer);
    tl->font(font);
    tl->text("Hello!");
    tl->name = tl->text();
    tl->rect(10,10,50,20);
    tl->textColor(blackColor);
    tl->backgroundColor = whiteColor;
    tl->cornerRadius = 8;
    ui->rootView->layer->addSublayer(tl);
  }
  {
    TextLayerPtr tl(new TextLayer);
    tl->font(font);
    tl->text("Second");
    tl->name = tl->text();
    tl->rect(75,10,50,20);
    tl->textColor(blackColor);
    tl->backgroundColor = whiteColor;
    tl->cornerRadius = 8;
    ui->rootView->layer->addSublayer(tl);
  }
  {
    TextLayerPtr tl(new TextLayer);
    tl->font(font);
    tl->text("More text, wrapping?");
    tl->name = tl->text();
    tl->rect(10,100,50,20);
    tl->textColor(blackColor);
    tl->backgroundColor = whiteColor;
    tl->cornerRadius = 8;
    ui->rootView->layer->addSublayer(tl);
  }
  {
    TextLayerPtr tl(new TextLayer);
    tl->font(font);
    tl->text("Batman");
    tl->name = tl->text();
    tl->rect(100,100,50,20);
    tl->textColor(blackColor);
    tl->backgroundColor = whiteColor;
    tl->cornerRadius = 8;
    ui->rootView->layer->addSublayer(tl);
  }
  first = true;
  logged = false;
}

void UiTestApp::update()
{
  glContext->clearColor(Color(.8,.8,.8,1));
  glContext->clear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);
  if(!first && !logged)
  {
    Application::instance()->resourceManager->logStats();
    logged = true;
  }
  if(first)
  {
    first = false;
  }
}

void UiTestApp::shutdown()
{
}

}
