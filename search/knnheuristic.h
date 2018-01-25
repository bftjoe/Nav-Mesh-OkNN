#pragma once
#include "searchnode.h"
#include "geometry.h"
#include "searchinstance.h"
#include "expansion.h"
#include "successor.h"
#include "mesh.h"
#include "point.h"
#include "cpool.h"
#include "timer.h"
#include "rtree.h"
#include "RStarTree.h"
#include "RStarTreeUtil.h"
#include <queue>
#include <vector>
#include <ctime>

namespace polyanya {

namespace rs = rstar;

class KnnHeuristic {
    typedef std::priority_queue<SearchNodePtr, std::vector<SearchNodePtr>,
                                PointerComp<SearchNode> > pq;
    typedef std::pair<Point, int> value;
    typedef bg::model::polygon<Point> polygon;
    private:
        int K = 1;
        warthog::mem::cpool* node_pool;
        MeshPtr mesh;
        Point start;
        std::vector<Point> goals;

        // kNN has k final node
        std::vector<SearchNodePtr> final_nodes;
        // poly_id: goal1, goal2, ...
        std::vector<std::vector<int>> end_polygons;
        // <i, v>: reached ith goal with cost v
        //std::map<int, double> reached;
        std::vector<double> reached;
        pq open_list;

        // Best g value for a specific vertex.
        std::vector<double> root_g_values;
        // Contains the current search id if the root has been reached by
        // the search.
        std::vector<int> root_search_ids;  // also used for root-level pruning
        int search_id;

        warthog::timer timer;
        warthog::timer angle_timer;
        std::vector<value> nn;

        // Pre-initialised variables to use in search().
        Successor* search_successors;
        SearchNode* search_nodes_to_push;
        //bgi::rtree<value, bgi::rstar<16> > rtree;

        void init() {
            verbose = false;
            search_successors = new Successor [mesh->max_poly_sides + 2];
            search_nodes_to_push = new SearchNode [mesh->max_poly_sides + 2];
            node_pool = new warthog::mem::cpool(sizeof(SearchNode));
            init_root_pruning();
        }

        void init_root_pruning() {
            assert(mesh != nullptr);
            search_id = 0;
            size_t num_vertices = mesh->mesh_vertices.size();
            root_g_values.resize(num_vertices);
            root_search_ids.resize(num_vertices);
        }

        /*
        int get_knn(const Point& p, int k) {
          nn.clear();
          rtree.query(bgi::nearest(p, k), std::back_inserter(nn));
          if (nn.empty()) return -1;
          else return nn.back().second;
        }

        int get_knn(const Point& l, const Point& r, int k=1) {
          nn.clear();
          bg::model::segment<Point> seg(l, r);
          rtree.query(bgi::nearest(seg, k), std::back_inserter(nn));
          if (nn.empty()) return -1;
          else return nn.back().second;
        }
        */

        rs::MinHeapEntry NearestInAreaAB(double angle0, double angle1, const Point& p, double curMin=INF);
        rs::MinHeapEntry NearestInAreaC(double angle0, double angle1, const Point& p, const Point& l, const Point& r, double curMin=INF);

        /*
         *   .........\.......p'......../...........
         *   ..........\....area:C...../............
         *   area:A     l-------------r.....area:B
         *   ........../....area:C'...\.............
         *   ........./.......p........\............
         *
         * A : nearest neighbour of point l
         * B : nearest neigbhour of point r
         * C : nearest neighbour of point p
         * C': nearest neighbour of point p'
         */
        std::pair<int, double> get_min_hueristic(const Point& p, const Point& l, const Point& r) {
          double minV = INF;
          int minArg = -1;

          auto updateRes = [&](rs::MinHeapEntry h, double dist) {
            if (h.key + dist < minV) {
              minArg = *((int*)h.entryPtr->data);
              minV = h.key + dist;
            }
          };

          if (is_collinear(p, l, r)) {
            rs::MinHeap heap;
            rs::Point P;
            if (p.distance(l) < p.distance(r))
              P = rs::Point(l.x, l.y);
            else
              P = rs::Point(r.x, r.y);
            double D = sqrt(rs::RStarTreeUtil::minDis2(P, rte->root->mbrn));
            heap.push(rs::MinHeapEntry(D, rte->root));

            rs::MinHeapEntry res(INF, (rs::Entry_P)nullptr);
            while (true) {
              res = rs::RStarTreeUtil::iNearestNeighbour(heap, P);
              if (res.key == INF) // not found
                break;
              int gid = *((int*)res.entryPtr->data);
              if (fabs(reached[gid] - INF) <= EPSILON)
                break;
              //if (reached.find(gid) == reached.end())
              //  break;
            }
            if (res.key != INF)
              updateRes(res, p.distance({P.coord[0], P.coord[1]}));
            return {minArg, minV};
          }

          Point p2, pl, pl2, pr, pr2;
          double pl_angle, pl2_angle, pr_angle, pr2_angle;

          p2 = reflect_point(p, l, r);

          pl = l - p, pl2 = l - p2;
          pr = r - p, pr2 = r - p2;
          angle_timer.start();
          pl_angle = get_angle(pl, true); pl2_angle = get_angle(pl2, true);
          pr_angle = get_angle(pr, true); pr2_angle = get_angle(pr2, true);
          angle_timer.stop();
          angle_using += angle_timer.elapsed_time_micro();

          rs::MinHeapEntry res = NearestInAreaAB(pl_angle, pl2_angle, l);
          updateRes(res, p.distance(l));

          res = NearestInAreaAB(pr2_angle, pr_angle, r, minV);
          updateRes(res, p.distance(r));

          res = NearestInAreaC(pr_angle, pl_angle, p, l, r, minV);
          updateRes(res, 0);

          res = NearestInAreaC(pl2_angle, pr2_angle, p2, r, l, minV);
          updateRes(res, 0);

          if (minArg == -1) {
            if ((int)final_nodes.size() != std::min(K, (int)goals.size()))
              assert(false);
          }
          return {minArg, minV};
        }

        /*
        Boost R-tree doesn't support within(polygon) query, so this function doesn't work currently
        std::pair<int, double> get_min_hueristic(const Point& r, const Point& a, const Point& b, int k=1) {
            Point perp = perp_point(r, a, b);
            Point r2 = reflect_point(r, a, b);
            Point v = r - perp;
            double t = 1e8;
            Point a0 = a + t * v, b0 = b + t * v, a1 = a - t * v, b1 = b - t * v;
            Point w = a - b;
            Point p0 = a0 + t * w, p1 = a1 + t * w, p2 = b0 - t * w, p3 = b1 - t * w;
            polygon A {{a0, a, b, b0, a0}};
            polygon B {{a, a1, b1, b, a}};
            polygon C {{p0, p1, a1, a0, p0}};
            polygon D {{b0, b1, p3, p2, b0}};
            int res = -1;
            double curv = 1e18; //INF

            auto is_better = [&](double& oldv, int gid) {
              double newv = get_h_value(r, goals[gid], a, b);
              if (oldv > newv) {
                oldv = newv;
                return true;
              }
              return false;
            };

            nn.clear();
            rtree.query(bgi::within(A) && bgi::nearest(r2, k), nn);
            if (!nn.empty() && is_better(curv, nn.back().second)) res = nn.back().second;

            nn.clear();
            rtree.query(bgi::within(B) && bgi::nearest(r, k), nn);
            if (!nn.empty() && is_better(curv, nn.back().second)) res = nn.back().second;

            nn.clear();
            rtree.query(bgi::within(C) && bgi::nearest(a, k), nn);
            if (!nn.empty() && is_better(curv, nn.back().second)) res = nn.back().second;

            nn.clear();
            rtree.query(bgi::within(D) && bgi::nearest(b, k), nn);
            if (!nn.empty() && is_better(curv, nn.back().second)) res = nn.back().second;

            return {res, curv};
        }
        */

        void initRtree() {
          rte = new rs::RStarTree();
          rtEntries.clear();
          gids.clear();
          for (int i=0; i<(int)goals.size(); i++) gids.push_back(i);
          for (int& i: gids) {
            const Point& it = goals[i];
            rs::Mbr mbr(it.x, it.x, it.y, it.y);
            rs::LeafNodeEntry leaf(mbr, (rs::Data_P)(&i));
            rtEntries.push_back(leaf);
          }

          for (auto& it: rtEntries)
            rte->insertData(&it);
        }

        void init_search() {
            assert(node_pool);
            node_pool->reclaim();
            search_id++;
            open_list = pq();
            final_nodes = std::vector<SearchNodePtr>();
            initRtree();
            reached.resize(goals.size());
            fill(reached.begin(), reached.end(), INF);
            angle_using = 0;
            nodes_generated = 0;
            nodes_pushed = 0;
            nodes_popped = 0;
            nodes_pruned_post_pop = 0;
            successor_calls = 0;
            set_end_polygon();
            gen_initial_nodes();
        }
        PointLocation get_point_location(Point p);
        void set_end_polygon();
        void gen_initial_nodes();
        int succ_to_node(
            SearchNodePtr parent, Successor* successors,
            int num_succ, SearchNodePtr nodes
        );

        void print_node(SearchNodePtr node, std::ostream& outfile);

    public:
        int nodes_generated;        // Nodes stored in memory
        int nodes_pushed;           // Nodes pushed onto open
        int nodes_popped;           // Nodes popped off open
        int nodes_pruned_post_pop;  // Nodes we prune right after popping off
        int successor_calls;        // Times we call get_successors
        bool verbose;
        double angle_using;
        rs::RStarTree* rte;
        std::vector<rs::LeafNodeEntry> rtEntries;
        std::vector<int> gids;

        KnnHeuristic() { }
        KnnHeuristic(MeshPtr m) : mesh(m) { init(); }
        KnnHeuristic(int k, MeshPtr m, Point s, std::vector<Point> gs) :
            K(k), mesh(m), start(s), goals(gs) { init(); }
        KnnHeuristic(KnnHeuristic const &) = delete;
        void operator=(KnnHeuristic const &x) = delete;
        ~KnnHeuristic() {
            if (node_pool) {
                delete node_pool;
            }
            delete[] search_successors;
            delete[] search_nodes_to_push;
            if (rte)
              delete rte;
        }

        void set_K(int k) { this->K = k; }

        void set_start_goal(Point s, std::vector<Point> gs) {
            start = s;
            goals = std::vector<Point>(gs);
            if (rte != nullptr)
              delete rte;

            /*
            rtree.clear();
            for (int i=0; i<(int)gs.size(); i++) {
              rtree.insert(std::make_pair(gs[i], i));
            }
            */
        }

        int search();

        double get_cost(int k) {
          if (k > (int)final_nodes.size()) {
            return -1;
          }
          return final_nodes[k]->f;
        }

        double get_search_micro()
        {
            return timer.elapsed_time_micro();
        }

        void get_path_points(std::vector<Point>& out, int k);
        void print_search_nodes(std::ostream& outfile, int k);
        void deal_final_node(const SearchNodePtr node);
        void gen_final_nodes(const SearchNodePtr node, const Point& rootPoint);

        double get_gid(int k) {
          if (k > (int)final_nodes.size()) return -1;
          else return final_nodes[k]->goal_id;
        }

        int get_goal_ord(int gid) {
          for (int i=0; i<K; i++) if (final_nodes[i]->goal_id == gid) return i;
          return -1;
        }
};

}
