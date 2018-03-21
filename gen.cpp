#include "EDBTObstacles.h"
#include "mesh.h"
#include "point.h"
#include "genPoints.h"
#include "park2poly.h"
#include <string>
#include <sstream>
#include <random>
using namespace std;
namespace pl = polyanya;

void gen_vg(string polypath, string meshpath) {
  ifstream meshfile(meshpath);
  ifstream polyfile(polypath);
  polyanya::Mesh* mp = new polyanya::Mesh(meshfile);
  EDBT::ObstacleMap* oMap = new EDBT::ObstacleMap(polyfile, mp, false);
  oMap->initVisObs();
  oMap->printObsMap();
}

void gen_poly(string parkpath, double size, int num) {
  vector<vector<polyanya::Point>> polys;
  ifstream parkfile(parkpath);
  polys = generator::read_polys(parkfile);
  if (num != -1)
    generator::random_choose_polys(polys, num);
  cerr << "size of polys: " << polys.size() << endl;

  //vector<vector<pl::Point>> normalized;
  generator::normalize_polys(polys, size);
  polys = generator::remove_bad_polys(polys);

  vector<vector<pl::Point>> simplified;
  generator::simplify_polys(polys, simplified);
  generator::print_polygons<long long>(simplified);
}

void gen_entities_points(string polypath, string meshpath, int num) {
  ifstream polyfile(polypath);
  ifstream meshfile(meshpath);
  polyanya::Mesh* mp = new polyanya::Mesh(meshfile);
  EDBT::ObstacleMap* oMap = new EDBT::ObstacleMap(polyfile, mp);

  // from *.poly2 -> *.poly
  if (polypath.back() == '2')
    polypath.pop_back();

  ifstream polysfile(polypath);
  vector<vector<pl::Point>> polys = generator::read_polys(polysfile);
  vector<pl::Point> out;
  generator::gen_points_in_traversable(oMap, polys, num, out, true);
}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    string t = string(argv[1]);
    if (t == "vg") {
      string polypath = string(argv[2]);
      string meshpath = string(argv[3]);
      gen_vg(polypath, meshpath);
    }
    else if (t == "pl") {
      string parkpath = string(argv[2]);
      string size = string(argv[3]);
      string num = string(argv[4]);
      gen_poly(parkpath, stod(size), stod(num));
    }
    else if (t == "pts") {
      string polypath = string(argv[2]);
      string meshpath = string(argv[3]);
      int num = atoi(argv[4]);
      gen_entities_points(polypath, meshpath, num);
    }
  }
  return 0;
}
