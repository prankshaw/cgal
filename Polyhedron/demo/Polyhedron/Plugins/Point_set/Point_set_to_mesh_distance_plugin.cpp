#include <CGAL/Three/Polyhedron_demo_plugin_interface.h>
#include <CGAL/Three/Polyhedron_demo_plugin_helper.h>
#include <QApplication>
#include <QDockWidget>
#include <QObject>
#include <QAction>
#include <QMainWindow>
#include <Scene_polyhedron_item.h>
#include <Scene_surface_mesh_item.h>
#include <Scene_points_with_normal_item.h>
#include "Messages_interface.h"
#include "Color_ramp.h"

#include "ui_Point_set_to_mesh_distance_widget.h"


#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_triangle_primitive.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/Kernel_traits.h>
#ifdef CGAL_LINKED_WITH_TBB
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/atomic.h>
#endif // CGAL_LINKED_WITH_TBB

#if defined(CGAL_LINKED_WITH_TBB)
template <class AABB_tree, class Point_3>
struct Distance_computation{
  const AABB_tree& tree;
  const Point_set & point_set;
  Point_3 initial_hint;
  tbb::atomic<double>* distance;
  std::vector<double>& output;

  Distance_computation(const AABB_tree& tree,
                       const Point_3 p,
                       const Point_set & point_set,
                       tbb::atomic<double>* d,
                       std::vector<double>& out )
    : tree(tree)
    , point_set(point_set)
    , initial_hint(p)
    , distance(d)
    , output(out)
  {
  }
  void
  operator()(const tbb::blocked_range<Point_set::const_iterator>& range) const
  {
    Point_3 hint = initial_hint;
    double hdist = 0;
    for( Point_set::const_iterator it = range.begin(); it != range.end(); ++it)
    {
      hint = tree.closest_point(point_set.point(*it), hint);
      Kernel::FT dist = squared_distance(hint,point_set.point(*it));
      double d = CGAL::sqrt(dist);
      output[std::size_t(*it)] = d;
      if (d>hdist) hdist=d;
    }

    if (hdist > distance->load())
      distance->store(hdist);
  }
};
#endif


template< class Mesh>
double compute_distances(const Mesh& m, const Point_set & point_set,
                         std::vector<double>& out)
{
  typedef CGAL::AABB_face_graph_triangle_primitive<Mesh> Primitive;
  typedef CGAL::AABB_traits<Kernel, Primitive> Traits;
  typedef CGAL::AABB_tree< Traits > Tree;

  Tree tree( faces(m).first, faces(m).second, m);
  tree.accelerate_distance_queries();
  tree.build();
  typedef typename boost::property_map<Mesh, boost::vertex_point_t>::const_type VPMap;
  VPMap vpmap = get(boost::vertex_point, m);
  typename Traits::Point_3 hint = get(vpmap, *vertices(m).begin());

#if !defined(CGAL_LINKED_WITH_TBB)
  double hdist = 0;
  for(Point_set::const_iterator it = point_set.begin();
      it != point_set.end(); ++it )
  {
    hint = tree.closest_point(point_set.point(*it), hint);
    Kernel::FT dist = squared_distance(hint,point_set.point(*it));
    double d = CGAL::sqrt(dist);
    out[std::size_t(*it)]= d;
    if (d>hdist) hdist=d;
  }
    return hdist;
#else
  tbb::atomic<double> distance;
  distance.store(0);
  Distance_computation<Tree, typename Traits::Point_3> f(tree, hint, point_set, &distance, out);
  tbb::parallel_for(tbb::blocked_range<Point_set::const_iterator>(point_set.begin(), point_set.end()), f);
  return distance;
#endif
}


using namespace CGAL::Three;

class DistanceWidget:
    public QDockWidget,
    public Ui::DistanceWidget
{
public:
  DistanceWidget(QString name, QWidget *parent)
    :QDockWidget(name,parent)
  {
    setupUi(this);
  }
};

class Point_set_to_mesh_distance_plugin :
    public QObject,
    public Polyhedron_demo_plugin_helper
{
  Q_OBJECT
  Q_INTERFACES(CGAL::Three::Polyhedron_demo_plugin_interface)
  Q_PLUGIN_METADATA(IID "com.geometryfactory.PolyhedronDemo.PluginInterface/1.0")
public:

  bool applicable(QAction*) const
  {
    if(!scene->numberOfEntries() == 2)
      return false;
    Scene_polyhedron_item* poly = NULL;
    Scene_surface_mesh_item* sm = NULL;
    Scene_points_with_normal_item* pn = NULL;
    Q_FOREACH(Scene_interface::Item_id i,scene->selectionIndices())
    {
      if(!poly)
        poly = qobject_cast<Scene_polyhedron_item*>(scene->item(i));
      if(!sm)
        sm = qobject_cast<Scene_surface_mesh_item*>(scene->item(i));
      if(!pn)
        pn = qobject_cast<Scene_points_with_normal_item*>(scene->item(i));
    }
    if((!poly && ! sm)|| !pn)
      return false;
    else
      return true;
  }

  QList<QAction*> actions() const
  {
    return _actions;
  }

  void init(QMainWindow* mainWindow, Scene_interface* sc, Messages_interface* mi)
  {

    this->messageInterface = mi;

    this->scene = sc;
    this->mw = mainWindow;


    QAction *actionDistance= new QAction(QString("Distance Mesh-Point Set"), mw);

    actionDistance->setProperty("submenuName", "Point_set");
    connect(actionDistance, SIGNAL(triggered()),
            this, SLOT(distance()));
    _actions << actionDistance;

    dock_widget = new DistanceWidget("Compute distance", mw);
    dock_widget->setVisible(false);
    addDockWidget(dock_widget);
    connect(dock_widget->pushButton, SIGNAL(clicked(bool)),
            this, SLOT(perform()));
  }
private Q_SLOTS:
  void perform()
  {
    Scene_polyhedron_item* poly = NULL;
    Scene_surface_mesh_item* sm = NULL;
    Scene_points_with_normal_item* pn = NULL;
    Q_FOREACH(Scene_interface::Item_id i,scene->selectionIndices())
    {
      if(!poly)
        poly = qobject_cast<Scene_polyhedron_item*>(scene->item(i));
      if(!sm)
        sm = qobject_cast<Scene_surface_mesh_item*>(scene->item(i));
      if(!pn)
        pn = qobject_cast<Scene_points_with_normal_item*>(scene->item(i));
    }
    if((!poly && ! sm)|| !pn)
      return ;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Color_ramp thermal_ramp;
    thermal_ramp.build_thermal();
    Fcolor_map fred_map;
    Fcolor_map fgreen_map;
    Fcolor_map fblue_map;
    Color_map red_map;
    Color_map green_map;
    Color_map blue_map;

    bool r, g, b;
     if(!pn->point_set()->check_colors())
     {
       //bind color pmaps
       boost::tie(fred_map  , r) = pn->point_set()->add_property_map<double>("red",0);
       boost::tie(fgreen_map, g)  = pn->point_set()->add_property_map<double>("green",0);
       boost::tie(fblue_map , b)  = pn->point_set()->add_property_map<double>("blue",0);
       //bind the new color maps to the right internal members so has_colors() return true in the item.
       pn->point_set()->check_colors();
     }
     else
     {
       boost::tie(fred_map  , r) =  pn->point_set()->property_map<double>("red");
       boost::tie(fgreen_map, g) =  pn->point_set()->property_map<double>("green");
       boost::tie(fblue_map , b) =  pn->point_set()->property_map<double>("blue");
       if(!r)
         red_map = pn->point_set()->property_map<unsigned char>("red").first;
       if(!g)
         green_map = pn->point_set()->property_map<unsigned char>("green").first;
       if(!b)
         blue_map = pn->point_set()->property_map<unsigned char>("blue").first;
     }
    Point_set* points = pn->point_set();
    points->collect_garbage();
    std::vector<double> distances(points->size());
    double hdist;
    if(poly)
      hdist = compute_distances(*poly->face_graph(), *points, distances);
    else
      hdist = compute_distances(*sm->face_graph(), *points, distances);
    if(hdist == 0)
      hdist++;

    int id=0;
    for (Point_set::const_iterator it = pn->point_set()->begin();
         it != pn->point_set()->end(); ++ it)
    {
      double d = distances[id]/hdist;
      //if r/g/b is false, it means the associated color map uses unsigned chars instead of doubles.
      if(r)
      fred_map[*it] = thermal_ramp.r(d) ;
      else
        red_map[*it]=static_cast<unsigned char>(thermal_ramp.r(d) * 255);
      if(g)
        fgreen_map[*it] =  thermal_ramp.g(d);
      else
        green_map[*it] = static_cast<unsigned char>(thermal_ramp.g(d) * 255);
      if(b)
        fblue_map[*it] = thermal_ramp.b(d);
      else
        blue_map[*it] = static_cast<unsigned char>(thermal_ramp.b(d) * 255);
      ++id;
    }

    dock_widget->minLabel->setText(QString("Minimum Distance: %1").arg(0));
    dock_widget->maxLabel->setText(QString("Maximum Distance: %1").arg(0));
    dock_widget->fDecileLabel->setText(QString("First Decile: %1").arg(0));
    dock_widget->lDecildeLabel->setText(QString("Last Decile: %1").arg(0));
    dock_widget->medianLabel->setText(QString("Median Distance: %1").arg(0));
    pn->invalidateOpenGLBuffers();
    pn->itemChanged();
    QApplication::restoreOverrideCursor();
  }
  void distance()
  {
    if(!dock_widget->isVisible())
    {
      dock_widget->show();
    }
    perform();
  }
  void closure()
  {
    dock_widget->hide();
  }
private:
  QList<QAction*> _actions;
  Messages_interface* messageInterface;
  DistanceWidget* dock_widget;
  typedef Point_set::Property_map<double> Fcolor_map;
  typedef Point_set::Property_map<unsigned char> Color_map;

};

#include "Point_set_to_mesh_distance_plugin.moc"
