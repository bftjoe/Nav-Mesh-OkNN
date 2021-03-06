#include "EDBTknn.h"
#include "RStarTreeUtil.h"
#include "consts.h"
#include <queue>

namespace EDBT {

using namespace std;
namespace rs = rstar;
typedef ObstacleMap::Vertex Vertex;
typedef polyanya::Point pPoint;
typedef ObstacleMap::Seg Seg;

void EDBTkNN::updateObstacles(set<pii> obs) {
  set<int> newV;
  for (pii it: obs) {
    if (!exploredV.count(it.first)) newV.insert(it.first);
    if (!exploredV.count(it.second)) newV.insert(it.second);
    if (this->get_current_micro() >= this->time_limit_micro) break;
  }
  // add edges between vertices
  for (int vid: exploredV) {
    Vertex v = getV(vid);
    for (int newid: newV) {
      Vertex newv = getV(newid);
      if (O->isVisible({v, newv})) {
        g.add_edge(v.id, newv.id, sqrt(Vertex::dist2(v, newv)));
      }
    }
    if (this->get_current_micro() >= this->time_limit_micro) break;
  }
  // add edges between new vertices
  for (int vid1: newV) {
    for (int vid2: newV) if (vid2 > vid1) {
      pii p{vid1, vid2};
      if (explored.count(p)) continue;
      const Vertex v1 = getV(vid1);
      const Vertex v2 = getV(vid2);
      if (O->isVisible({v1, v2}))
        g.add_edge(v1.id, v2.id, sqrt(Vertex::dist2(v1, v2)));
      if (this->get_current_micro() >= this->time_limit_micro) break;
    }
    if (this->get_current_micro() >= this->time_limit_micro) break;
  }
  for (int vid: newV) {
    Vertex v = getV(vid);
    pPoint p = pPoint{(double)v.x, (double)v.y};
    // add edges between (s, v);
    if (O->isVisible(g.start, p)) {
      g.add_edge(v.id, g.sid(), p.distance(g.start));
    }
    // add edges between (t, v);
    if (O->isVisible(g.goal, p)) {
      g.add_edge(v.id, g.tid(), p.distance(g.goal));
    }
    if (this->get_current_micro() >= this->time_limit_micro) break;
  }
  exploredV.insert(newV.begin(), newV.end());
}

void EDBTkNN::changeTarget(pPtr p) {
  // remove previous edges
  for (const auto& it: g.es[g.tid()]) {
    int to = it.first;
    assert(g.es[to].count(g.tid()));
    auto rmIt = g.es[to].find(g.tid());
    g.es[to].erase(rmIt);
  }
  g.es[g.tid()].clear();
  g.vs[g.tid()] = g.goal = *p;
  for (int vid: exploredV) {
    Vertex v = getV(vid);
    pPoint vp = pPoint{(double)v.x, (double)v.y};
    if (O->isVisible(g.goal, vp)) {
      g.add_edge(vid, g.tid(), vp.distance(g.goal));
    }
    if (this->get_current_micro() >= this->time_limit_micro) break;
  }
  // try to add edge between start and end
  if (O->isVisible(*p, q)) {
    g.add_edge(g.sid(), g.tid(), p->distance(q));
  }
}

double Graph::Dijkstra(double r, const set<int>& exploredV) {
  for (int i: exploredV) {
    dist[i] = INF;
    pre[i] = -1;
  }
  dist[sid()] = 0;
  dist[tid()] = INF;
  pre[sid()] = -1;
  pre[tid()] = -1;
  path_ids.clear();
  // <dist, vid>
	open_list = pq();
	this->nodes_generated++;
  open_list.push({0, sid()});
  double res = INF;
  while (!open_list.empty()) {
    pair<double, int> c = open_list.top(); open_list.pop();
    if (c.first - EPSILON > dist[c.second]) continue;
    if (c.second == tid()) {
      res = c.first;
      int last_id = c.second;
      while (last_id != -1) {
        path_ids.push_back(last_id);
        last_id = pre[last_id];
      }
      reverse(path_ids.begin(), path_ids.end());
      assert(path_ids.front() == sid());
      break;
    }
    // because all segments touch the ring will be retrieved
    // if all segments are strictly in explored area:
    //  1. a path has been found
    //  2. terminate with res=INF (not reachable)
    // otherwise there is a `c` in queue that c.first >= r
    if (c.first > r + EPSILON)
      res = min(res, c.first);

    for (const auto& it: es[c.second]) {
      double nxtd = c.first + it.second;
      if (nxtd < dist[it.first]) {
        dist[it.first] = nxtd;
        pre[it.first] = c.second;
				this->nodes_generated++;
        open_list.push({nxtd, it.first});
      }
    }
  }
	while (!open_list.empty()) open_list.pop();
  return res;
}

void EDBTkNN::enlargeExplored(double preR, double newR) {
  vector<rs::Data_P> rawObs;
  rs::RStarTreeUtil::rangeQuery(O->rtree, rs::Point(q.x, q.y), preR, newR, rawObs);
  set<pii> obs;
  for (auto itPtr: rawObs) {
    Seg seg = *((Seg*)itPtr);
    obs.insert({seg.first.id, seg.second.id});
  }
  updateObstacles(obs);
}

double EDBTkNN::ODC(Graph& g, pPtr p, double& curR) {
  // before call this function, Graph g must be initilized
  // if didn't initialized with goal=p, change target
  if (g.goal.distance(*p) > EPSILON)
    changeTarget(p);
  if (curR <= EPSILON) { // first time call ODC
    double r = p->distance(q);
    enlargeExplored(0, r);
  }
  double d = INF;
  do {
    d = g.Dijkstra(curR, exploredV);
    if (d <= curR) // find valid shortest path
      break;
    else if (fabs(d - INF) <= EPSILON) // not reachable
      break;
    else if (this->get_current_micro() >= this->time_limit_micro)
      break;
    else { // d > curR
      enlargeExplored(curR, d);
      if (this->get_current_micro() >= this->time_limit_micro) break;
      curR = d;
    }
  } while (true);
  return d;
}

vector<pair<pPtr, double>> EDBTkNN::OkNN(int k) {
  initSearch();
  timer.start();
  vector<pair<pPtr, double>> res;
  vector<pair<pPtr, double>> ps = Euclidean_NN(k);
  priority_queue<pair<double, pPtr>, vector<pair<double, pPtr>>, less<pair<double, pPtr>>> que;
  if (ps.empty()) return res;
  pPtr p_ = ps.back().first;
  double r = p_->distance(q);
  double dmax;
  enlargeExplored(0, r);
  // `pPtr` is the pointer to goal point
  // vector<int> is the sequence of vert ids of the path
  map<pPtr, vector<int>> path_to_goals;
  vector<int> curP;
  for (auto& it: ps) {
    it.second = ODC(g, it.first, r);
    path_to_goals[it.first] = vector<int>(g.path_ids);
  }
  auto cmp = [&](pair<pPtr, double>& lhs, pair<pPtr, double>& rhs) {
    return lhs.second < rhs.second;
  };
  sort(ps.begin(), ps.end(), cmp);
  dmax = ps.back().second;
  for (auto& it: ps) {
		que.push({it.second, it.first});
		this->g.nodes_generated++;
	}
  do {
    pair<pPtr, double> nxt = next_Euclidean_NN();
    if(nxt.first == nullptr) break;
    double d_o = ODC(g, nxt.first, r);
    if (d_o < que.top().first) {
      que.pop();
      que.push({d_o, nxt.first});
			this->g.nodes_generated++;
      dmax = que.top().first;
      path_to_goals[nxt.first] = vector<int>(g.path_ids);
    }
    if (nxt.second > dmax) break;
    if (this->get_search_micro() >= this->time_limit_micro) break;
  } while (true);
  while (!que.empty()) {
    res.push_back({que.top().second, que.top().first});
    if (!path_to_goals[res.back().first].empty())
      paths.push_back(to_point_path(path_to_goals[res.back().first], res.back().first));
    que.pop();
  }
  if (!res.empty())
    reverse(res.begin(), res.end());
  if (!paths.empty())
    reverse(paths.begin(), paths.end());
  timer.stop();
  return res;
}

vector<pair<pPtr, double>> EDBTkNN::Euclidean_NN(int k) {
  heap.clear();
  double d;
  rs::Point rq(q.x, q.y);
  d = sqrt(rs::RStarTreeUtil::dis2(rq, rte->root->mbrn));
  heap.push(rs::MinHeapEntry(d, rte->root));
  vector<pair<pPtr, double> > res;
  for (int i=0; i<k; i++) {
    rs::MinHeapEntry e = rs::RStarTreeUtil::iNearestNeighbour(heap, rs::Point(q.x, q.y));
    if (e.entryPtr == nullptr) break;
    pPtr p = (pPoint*)e.entryPtr->data;
    d = e.key;
    res.push_back({p, d});
  }
  return res;
}

pair<pPtr, double> EDBTkNN::next_Euclidean_NN() {
  rs::MinHeapEntry e = rs::RStarTreeUtil::iNearestNeighbour(heap, rs::Point(q.x, q.y));
  if (e.entryPtr == nullptr) return {nullptr, INF};
  else {
    pPtr p = (pPoint*)e.entryPtr->data;
    return {p, e.key};
  }
}

}// namespace EDBT
