#pragma once
#include "searchnode.h"
#include "successor.h"
#include "mesh.h"
#include "point.h"
#include "cpool.h"
#include <queue>
#include <vector>
#include <ctime>

namespace polyanya
{

template<typename T, typename Compare = std::greater<T> >
struct PointerComp
{
    bool operator()(const T* x,
                    const T* y) const
    {
        return Compare()(*x, *y);
    }
};

typedef std::shared_ptr<Mesh> MeshPtr;

class SearchInstance
{
    typedef std::priority_queue<SearchNodePtr, std::vector<SearchNodePtr>,
                                PointerComp<SearchNode> > pq;
    private:
        warthog::mem::cpool* node_pool;
        MeshPtr mesh;
        Point start, goal;

        SearchNodePtr final_node;
        int end_polygon; // set by init_search
        pq open_list;

        // Best g value for a specific vertex.
        std::vector<double> root_g_values;
        // Contains the current search id if the root has been reached by
        // the search.
        std::vector<int> root_search_ids;  // also used for root-level pruning
        int search_id;

        clock_t search_time;

        int nodes_expanded;
        int nodes_generated;

        void init()
        {
            node_pool = new warthog::mem::cpool(sizeof(SearchNode));
            init_root_pruning();
        }
        void init_root_pruning()
        {
            assert(mesh != nullptr);
            search_id = 0;
            size_t num_vertices = mesh->mesh_vertices.size();
            root_g_values.resize(num_vertices);
            root_search_ids.resize(num_vertices);
        }
        void init_search()
        {
            assert(node_pool);
            node_pool->reclaim();
            search_id++;
            open_list = pq();
            final_node = nullptr;
            nodes_expanded = 0;
            nodes_generated = 0;
            set_end_polygon();
            gen_initial_nodes();
        }
        PointLocation get_point_location(Point p);
        void set_end_polygon();
        void gen_initial_nodes();
        int succ_to_node(
            SearchNodePtr parent, std::vector<Successor>& successors,
            int num_succ, std::vector<SearchNodePtr>& nodes
        );

    public:
        SearchInstance() { }
        SearchInstance(MeshPtr m) : mesh(m) { init(); }
        SearchInstance(MeshPtr m, Point s, Point g) :
            mesh(m), start(s), goal(g) { init(); }
        SearchInstance(SearchInstance const &) = delete;
        void operator=(SearchInstance const &x) = delete;
        ~SearchInstance()
        {
            if (node_pool)
            {
                delete node_pool;
            }
        }

        void set_start_goal(Point s, Point g)
        {
            start = s;
            goal = g;
            final_node = nullptr;
        }

        bool search();
        double get_cost()
        {
            if (final_node == nullptr)
            {
                return -1;
            }

            return final_node->f;
        }

        double get_search_micro()
        {
            return ((double) search_time) / CLOCKS_PER_SEC * 1e6;
        }

        int get_nodes_expanded()
        {
            return nodes_expanded;
        }

        int get_nodes_generated()
        {
            return nodes_generated;
        }

        void get_path_points(std::vector<Point>& out);
        void print_search_nodes(std::ostream& outfile);

};

}
