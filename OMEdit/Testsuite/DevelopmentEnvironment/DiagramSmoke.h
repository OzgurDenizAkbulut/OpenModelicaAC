#ifndef DIAGRAMSMOKE_H
#define DIAGRAMSMOKE_H

#include "MainWindow.h"
#include "Element/Element.h"
#include "Modeling/LibraryTreeWidget.h"
#include "Modeling/ModelWidgetContainer.h"
#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QTemporaryDir>
#include <QToolButton>
#include <stdexcept>

inline void checkDiagram(const QString &output)
{
  auto check = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
    qInfo().noquote() << "PASS:" << message;
  };
  QTemporaryDir workspace(output + "/diagram-XXXXXX");
  check(workspace.isValid(), "diagram fixture has an isolated directory");
  QFile file(workspace.filePath("DiagramRenderAcceptance.mo"));
  const QByteArray source = R"modelica(package DiagramRenderAcceptance
  connector Signal
    Real value;
    annotation(Icon(graphics={Ellipse(extent={{-100,100},{100,-100}},
      lineColor={0,70,160}, fillColor={80,160,240}, fillPattern=FillPattern.Solid)}));
  end Signal;
  model Node
    Signal port annotation(Placement(
      transformation(origin={100,0}, extent={{-10,-10},{10,10}}),
      iconTransformation(origin={100,0}, extent={{-10,-10},{10,10}})));
    annotation(Icon(graphics={Rectangle(extent={{-100,100},{100,-100}},
      lineColor={0,70,160}, fillColor={220,235,250}, fillPattern=FillPattern.Solid),
      Text(extent={{-90,40},{90,-40}}, textString="%name")}));
  end Node;
  model Diagram
    Node left annotation(Placement(transformation(origin={-50,0}, extent={{-20,-20},{20,20}})));
    Node right annotation(Placement(transformation(origin={50,0}, extent={{-20,-20},{20,20}}, rotation=180)));
  equation
    connect(left.port, right.port) annotation(Line(points={{-30,0},{30,0}}, color={0,70,160}));
    annotation(Diagram(coordinateSystem(extent={{-100,-100},{100,100}})));
  end Diagram;
end DiagramRenderAcceptance;
)modelica";
  check(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "diagram fixture written");
  file.close();
  MainWindow *main = MainWindow::instance();
  main->switchToModelingPerspectiveSlot();
  main->getLibraryWidget()->openFile(file.fileName());
  LibraryTreeModel *tree = main->getLibraryWidget()->getLibraryTreeModel();
  LibraryTreeItem *item = tree->findLibraryTreeItem("DiagramRenderAcceptance.Diagram");
  check(item != nullptr, "diagram fixture loads through the real Library Browser");
  tree->showModelWidget(item);
  ModelWidget *model = item->getModelWidget();
  check(model != nullptr, "diagram fixture creates a real model widget");
  model->getDiagramViewToolButton()->setChecked(true);
  main->resize(1200, 800);
  main->show();
  qApp->processEvents();
  GraphicsView *diagram = model->getDiagramGraphicsView();
  check(diagram && model->isDiagramViewLoaded() && diagram->isVisible(), "real diagram view is loaded and visible");
  Element *left = diagram->getElementObject("left");
  Element *right = diagram->getElementObject("right");
  check(left && right && left->getModelComponent() && right->getModelComponent(),
        "real diagram contains both compiler-backed placed components");
  check(left->scene() == diagram->scene() && right->scene() == diagram->scene()
        && left->isVisible() && right->isVisible() && left->hasShapeAnnotation() && right->hasShapeAnnotation(),
        "placed component icon annotations are present in the real graphics scene");
  check(left->sceneBoundingRect().center().x() < right->sceneBoundingRect().center().x()
        && diagram->getConnectionsList().size() == 1, "diagram preserves component placement and the connector line");
  check(diagram->viewport()->grab().save(output + "/model-diagram.png"), "real model diagram screenshot saved");
}

#endif
