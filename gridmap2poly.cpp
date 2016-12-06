/*
Takes a map file from stdin, and outputs a polygon map file on stdout.
A polygon map file is as defined:
	The first line is "poly".
	The second line is the version of the format (currently 1).
	The third line contains an integer, N, which is how many polys there are.
	Then follows N lines.
		The below will all be separated by spaces.
		The first integer M is how many points there are in the poly.
		Then follows 2*M (possibly non-integer) numbers in the form
			x1 y1 x2 y2 [...] xM yM
		such that the ith point has coordinates {xi, yi}.

	By convention, the first polygon should be an axis-aligned rectangle
	corresponding to the whole map.
	Assume that the polygons do not contain any self-intersections.
	To get the traversable area, take the symmetric difference of ALL polygons.

To do this, we do a floodfill from the edge of the map to assign a "elevation"
to all grid squares. To do this, we first assume that every square outside of
the map is traversable.
The elevation of a point is the minimum number of "traversability changes"
needed to get there. That is:
- any traversable area which is connected to a cell outside of the map has an
  elevation of 0
- any obstacle connected to a 0-elevation traversable cell has an elevation of 1
- any traversable area connected to a 1-elevation obstacle, but not connected to
  the outside of the map, has elevation 2.
- any obstacle connected to a 2-elevation traversable cell has an elevation of 3
and so on.
We then create the polygons from the "edges" where the elevation changes.
We do not create the first polygon as stated above, as that is trivial and
it brings in some edge cases.
Note that no "edge" is shared between two polygons, with the exception of the
edge of the map.

Can imagine the process as a Dijkstra though the graph of the grid, such that
whenever the traversability changes, it has a weight of 1, else, it has a
weight of 0.
*/
#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <unordered_map>
#include <vector>
#include <queue>
#include <stdlib.h>
#include <cassert>



typedef std::vector<bool> vbool;
typedef std::vector<int> vint;

// Below is used for the data structure for
// {point on map : {polygon id : (point1, point2)}}
// for generating the polygons.
typedef std::pair<int, int> point;
typedef std::vector<point> vpoint;
typedef std::map<int, vpoint> int_to_vpoint;
typedef std::vector<int_to_vpoint> vint_to_vpoint;

// Search node used for the Dijkstra-like floodfill.
// We want to prioritise search nodes with a lower elevation, then the ones
// which have an ID (compared to the ones which have an ID of -1).
struct search_node
{
	int elevation;
	int id;
	point pos;

	bool operator<(const search_node& rhs) const
	{
		// We want the lowest elevations first.
		if (elevation != rhs.elevation)
		{
			return elevation < rhs.elevation;
		}
		// Then we want the HIGHEST IDs first to avoid -1s.
		return id > rhs.id;
	}


	bool operator>(const search_node& rhs) const
	{
		return rhs < *this;
	}
};

const int DX[] = {-1, 1, 0, 0};
const int DY[] = {0, 0, -1, 1};


// Globals
// From the map
std::vector<vbool> map_traversable;
int map_width, map_height;

// Generated by program
int next_id = 0;
std::vector<vint> polygon_id;
std::vector<int> id_to_elevation; // resize as necessary
std::vector<point> id_to_first_point; // resize with above
std::vector<vint_to_vpoint> id_to_neighbours;


void fail(std::string msg)
{
	std::cerr << msg << std::endl;
	exit(1);
}

void read_map()
{
	// Most of this code is from dharabor's warthog.
	// read in the whole map. ensure that it is valid.
	std::unordered_map<std::string, std::string> header;

	// header
	for (int i = 0; i < 3; i++)
	{
		std::string hfield, hvalue;
		if (std::cin >> hfield)
		{
			if (std::cin >> hvalue)
			{
				header[hfield] = hvalue;
			}
			else
			{
				fail("err; map has bad header");
			}
		}
		else
		{
			fail("err; map has bad header");
		}
	}

	if (header["type"] != "octile")
	{
		fail("err; map type is not octile");
	}

	// we'll assume that the width and height are less than INT_MAX
	map_width = atoi(header["width"].c_str());
	map_height = atoi(header["height"].c_str());

	if (map_width == 0 || map_height == 0)
	{
		fail("err; map has bad dimensions");
	}

	// we now expect "map"
	std::string temp_str;
	std::cin >> temp_str;
	if (temp_str != "map")
	{
		fail("err; map does not have 'map' keyword");
	}


	// basic checks passed. initialse the map
	map_traversable = std::vector<vbool>(map_height, vbool(map_width));
	// so to get (x, y), do map_traversable[y][x]
	// 0 is nontraversable, 1 is traversable

	// read in map_data
	int cur_y = 0;
	int cur_x = 0;

	char c;
	while (std::cin.get(c))
	{
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			// whitespace.
			// cannot put in the switch statement below as we need to check
			// "too many chars" before anything else
			continue;
		}

		if (cur_y == map_height)
		{
			fail("err; map has too many characters");
		}

		switch (c)
		{
			case 'S':
			case 'W':
			case 'T':
			case '@':
			case 'O':
				// obstacle
				map_traversable[cur_y][cur_x] = 0;
				break;
			default:
				// traversable
				map_traversable[cur_y][cur_x] = 1;
				break;
		}

		cur_x++;
		if (cur_x == map_width)
		{
			cur_x = 0;
			cur_y++;
		}
	}

	if (cur_y != map_height || cur_x != 0)
	{
		fail("err; map has too few characters");
	}
}


void get_id_and_elevation()
{
	// Initialise polygon_id with -1s.
	polygon_id = std::vector<vint>(map_height, vint(map_width, -1));
	// Initialise id_to_elevation as empty vint.
	id_to_elevation.clear();

	// Do a Dijkstra-like floodfill. Need an "open list".
	// We want to prioritise search nodes with a lower elevation, then the ones
	// which have an ID.
	// typedef std::pair<int, point> search_node;
	std::priority_queue<search_node,
		std::vector<search_node>,
		std::greater<search_node>> open_list;

	// Initialise open list.
	// Go around edge of map and add in points: elevation 0 if traversable,
	// 1 if not.

	// Do the top row and bottom row first.
	const int bottom_row = map_height - 1;
	for (int i = 0; i < map_width; i++)
	{
		open_list.push({!map_traversable[0][i], -1, {i, 0}});
		open_list.push({!map_traversable[bottom_row][i], -1, {i, bottom_row}});
	}

	// Then do the left and right columns.
	// Omit the top row and bottom row.
	const int right_col = map_width - 1;
	for (int i = 1; i < bottom_row; i++)
	{
		open_list.push({!map_traversable[i][0], -1, {0, i}});
		open_list.push({!map_traversable[i][right_col], -1, {right_col, i}});
	}

	while (!open_list.empty())
	{
		search_node c = open_list.top(); open_list.pop();
		const int x = c.pos.first, y = c.pos.second;
		if (polygon_id[y][x] != -1)
		{
			// Already seen before, skip.
			continue;
		}
		//std::cerr << x << " " << y << std::endl;
		if (c.id == -1)
		{
			// Give it a new ID.
			c.id = next_id++;
			id_to_elevation.push_back(c.elevation);
			id_to_first_point.push_back(c.pos);
		}
		polygon_id[y][x] = c.id;

		// Go through all neighbours.
		for (int i = 0; i < 4; i++)
		{
			const int next_x = x + DX[i], next_y = y + DY[i];
			if (next_x < 0 || next_x >= map_width ||
				next_y < 0 || next_y >= map_height)
			{
				continue;
			}


			if (polygon_id[next_y][next_x] != -1)
			{
				// Already seen before, skip.
				// Checking this here is optional, but speeds up run time.
				continue;
			}


			if (map_traversable[y][x] == map_traversable[next_y][next_x])
			{
				// same elevation, same id
				open_list.push({c.elevation, c.id, {next_x, next_y}});
			}
			else
			{
				// new elevation, new id
				// may have been traversed before but that case is handled above
				open_list.push({c.elevation + 1, -1, {next_x, next_y}});
			}
		}
	}
}

void make_edges()
{
	// Fill in id_to_neighbours, which, for each lattice point, is a mapping
	// from an ID to the two neighbouring lattice points where the polygon
	// is connected to.

	id_to_neighbours = std::vector<vint_to_vpoint>(
							map_height + 1, vint_to_vpoint(map_width + 1));

	// First, iterate over each "horizontal" edge made by two vertically
	// adjacent cells. This includes cells "outside" of the map which we will
	// assume to be traversable and have a elevation of 0.

	// First, iterate over the y position of the horizontal edge.
	for (int edge = 0; edge < map_height + 1; edge++)
	{
		// The interesting cells we are looking for have a y position of
		// edge-1 and edge respectively.
		// Then we can iterate over the x values of the cells as normal.
		const bool is_top = edge == 0;
		const bool is_bot = edge == map_height;
		for (int x = 0; x < map_width; x++)
		{
			const int top_id = (is_top ? -1 : polygon_id[edge - 1][x]);
			const int bot_id = (is_bot ? -1 : polygon_id[edge][x]);
			const int top_ele = (is_top ? 0 : id_to_elevation[top_id]);
			const int bot_ele = (is_bot ? 0 : id_to_elevation[bot_id]);

			if (top_ele == bot_ele)
			{
				// Same elevation, therefore no edge will be made.
				continue;
			}
			const int id_of_edge = (top_ele > bot_ele ? top_id : bot_id);
			assert(id_of_edge != -1);

			// Now we got an edge and the ID it's correlated to.
			// For both points, we add the other point to the neighbours.
			id_to_neighbours[edge][x][id_of_edge].push_back({x + 1, edge});
			id_to_neighbours[edge][x + 1][id_of_edge].push_back({x, edge});
		}
	}

	// Now we iterate over the "vertical" edges made by two horizontally
	// adjacent cells.

	for (int edge = 0; edge < map_width + 1; edge++)
	{
		const bool is_left = edge == 0;
		const bool is_right = edge == map_width;
		for (int y = 0; y < map_height; y++)
		{
			const int left_id = (is_left ? -1 : polygon_id[y][edge - 1]);
			const int right_id = (is_right ? -1 : polygon_id[y][edge]);
			const int left_ele = (is_left ? 0 : id_to_elevation[left_id]);
			const int right_ele = (is_right ? 0 : id_to_elevation[right_id]);

			if (left_ele == right_ele)
			{
				continue;
			}
			const int id_of_edge = (left_ele > right_ele ? left_id : right_id);
			assert(id_of_edge != -1);

			id_to_neighbours[y][edge][id_of_edge].push_back({edge, y + 1});
			id_to_neighbours[y + 1][edge][id_of_edge].push_back({edge, y});
		}
	}
}

void print_map()
{
	for (auto row : map_traversable)
	{
		for (auto t : row)
		{
			std::cout << "X."[t];
		}
		std::cout << std::endl;
	}
}

void print_elevation()
{
	for (auto row : polygon_id)
	{
		for (int id : row)
		{
			std::cout << id_to_elevation[id];
		}
		std::cout << std::endl;
	}
}

void print_ids()
{
	for (auto row : polygon_id)
	{
		for (int id : row)
		{
			std::cout << id << " ";
		}
		std::cout << std::endl;
	}
}

int main()
{
	using namespace std;
	read_map();
	cerr << "map done" << endl;
	get_id_and_elevation();
	cerr << "elevation done" << endl;
	make_edges();
	cerr << "edges done" << endl;
	//print_map();
	print_ids();
	print_elevation();

	return 0;
}
