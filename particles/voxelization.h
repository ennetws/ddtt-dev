// Adapted from https://github.com/Forceflow/ooc_svo_builder
#pragma once
#include <stack>
#include <deque>
#include <set>

#include <Eigen/Core>
#include "SurfaceMeshModel.h"

#include "morton.h"

template <typename T>
struct AABox {
	T min;
	T max;
	AABox(): min(T()), max(T()){}
	AABox(T min, T max): min(min), max(max){}

	// Assumes 'T' is floating point
	AABox(AABox a, AABox b){
		min = T(DBL_MAX, DBL_MAX, DBL_MAX);
		max = -min;
		for(int i = 0; i < 3; i++){
			min[i] = std::min( min[i], std::min( a.min[i], b.min[i] ) );
			max[i] = std::max( max[i], std::max( a.max[i], b.max[i] ) );
		}
	}
};

template<typename Vector3>
struct BasicTriangle{
	BasicTriangle() { counter = 0; v0_color = v1_color = v2_color = Vector3(0,0,0); }
	void setPoint(Vector3 p){ if(counter == 0) v0 = p; if(counter == 1) v1 = p; if(counter == 2) v2 = p; counter++; }
	Vector3 v0, v1, v2;
	Vector3 v0_color, v1_color, v2_color;
	int counter;
};

// Intersection methods
template<typename Vector3>
inline AABox<Vector3> computeBoundingBox(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2){
	AABox<Vector3> answer; 
	answer.min[0] = std::min(v0[0],std::min(v1[0],v2[0]));
	answer.min[1] = std::min(v0[1],std::min(v1[1],v2[1]));
	answer.min[2] = std::min(v0[2],std::min(v1[2],v2[2]));
	answer.max[0] = std::max(v0[0],std::max(v1[0],v2[0]));
	answer.max[1] = std::max(v0[1],std::max(v1[1],v2[1]));
	answer.max[2] = std::max(v0[2],std::max(v1[2],v2[2]));
	return answer;
}

template<typename Vector3>
inline Vector3 average3Vec(const Vector3 v0, const Vector3 v1, const Vector3 v2){
	Vector3 answer;
	for (size_t i = 0; i < 3; i++){
		answer[i] = (v0[i] + v1[i] + v2[i]) / 3.0;
	}
	return answer;
}

template <typename T> T clampval(const T& value, const T& low, const T& high) {
  return value < low ? low : (value > high ? high : value); 
}

#define EMPTY_VOXEL 0
#define FULL_VOXEL 1
#define OUTER_VOXEL 2

#define X_ 0
#define Y_ 1
#define Z_ 2

// This struct defines VoxelData for our voxelizer.
// This is the main memory hogger: the less data you store here, the better.
template<typename Vector3>
struct VoxelData{
	uint64_t morton;
	Vector3 color;
	Vector3 normal;
	bool isOuter;
	
	VoxelData() : morton(0), normal(Vector3()), color(Vector3()), isOuter(true){}
	VoxelData(uint64_t morton, bool isOuter, Vector3 normal = Vector3(0,0,0), Vector3 color = Vector3(0,0,0)) :
		morton(morton), isOuter(isOuter), normal(normal), color(color){}

	bool operator > (const VoxelData &a) const{	return morton > a.morton; }
	bool operator < (const VoxelData &a) const{	return morton < a.morton; }
};

template<typename Vector3>
struct VoxelContainer{
	std::vector< VoxelData<Vector3> > data, aux;
	Vector3 translation;
	double unitlength;
	size_t gridsize;
	bool isSolid;
	std::vector<char> occupied;
	std::vector< std::vector<Vector3> > quads;
	VoxelContainer() : translation(Vector3(0,0,0)), unitlength(-1), gridsize(-1){}
	std::vector< Vector3 > voxelCenters(){
		std::vector< Vector3 > result;
		for(auto voxel : data) result.push_back( voxelPos(voxel.morton) );
		return result;
	}
	inline Vector3 voxelPos( uint64_t m ){
		Vector3 delta = translation + ( 0.5 * Vector3(unitlength,unitlength,unitlength) );
		unsigned int v[3];
		mortonDecode(m, v[0], v[1], v[2]);
		return Vector3(v[2] * unitlength, v[1] * unitlength, v[0] * unitlength) + delta;
	}
	void findOccupied(){ occupied.resize(gridsize*gridsize*gridsize, EMPTY_VOXEL); for(auto & v : data) occupied[v.morton] = FULL_VOXEL; }
	std::vector< Vector3 > pointsOutside( double alpha = 0.0 )
	{
		std::vector< Vector3 > result;

		Vector3 delta = translation + ( 0.5 * Vector3(unitlength,unitlength,unitlength) );

		if(occupied.empty()) findOccupied();

		for(auto voxel : data)
		{					
			unsigned int x,y,z;
			mortonDecode(voxel.morton, x, y, z);
			Vector3 curVoxelPos = voxelPos(voxel.morton);

			std::vector<uint64_t> neigh;
			if(x < gridsize - 1) neigh.push_back( mortonEncode_LUT(x+1,y,z) );
			if(y < gridsize - 1) neigh.push_back( mortonEncode_LUT(x,y+1,z) );
			if(z < gridsize - 1) neigh.push_back( mortonEncode_LUT(x,y,z+1) );
			if(x > 0) neigh.push_back( mortonEncode_LUT(x-1,y,z) );
			if(y > 0) neigh.push_back( mortonEncode_LUT(x,y-1,z) );
			if(z > 0) neigh.push_back( mortonEncode_LUT(x,y,z-1) );

			// Voxels at surface
			for(auto n : neigh){
				if(occupied[n] != occupied[voxel.morton]){
					// We add center of empty neighbors
					Vector3 nPos = voxelPos(n);
					Vector3 midPoint = (nPos + curVoxelPos) * 0.5;
					result.push_back( ((1-alpha) * nPos) + (alpha * midPoint) );
				}
			}

			// Case where voxel is at the boundary of grid
			unsigned int v[] = { x, y, z };
			bool isBoundary = (x == 0 || y == 0 || z == 0) || (v[0] == gridsize-1 || v[1] == gridsize-1 || v[2] == gridsize-1);
			if( isBoundary ){
				for(int i = 0; i < 3; i++){
					Eigen::Vector3i d(0,0,0);
					if(v[i] == 0) d[i] = -1;
					else if(v[i] == gridsize-1) d[i] = 1;
					if(d[0]!=0||d[1]!=0||d[2]!=0){
						d += Eigen::Vector3i(x,y,z);
						Vector3 nPos = Vector3(d[2] * unitlength, d[1] * unitlength, d[0] * unitlength) + delta;
						Vector3 midPoint = (nPos + curVoxelPos) * 0.5;
						result.push_back( ((1-alpha) * nPos) + (alpha * midPoint) );
					}
				}
			}
		}

		return result;
	}
	inline bool isValidGridPoint( const Eigen::Vector3i& gridpnt ) const{
		if(gridpnt.x() < 0 || gridpnt.y() < 0 || gridpnt.z() < 0) return false;
		if(gridpnt.x() > gridsize-1 || gridpnt.y() > gridsize-1 || gridpnt.z() > gridsize-1) return false;
		return true;
	}
};

template<typename Vector3>
inline std::vector<Vector3> voxelQuad(Eigen::Vector3i direction, double length = 1.0)
{
	static double n[6][3] = {{-1.0,  0.0, 0.0},{0.0, 1.0, 0.0},{1.0, 0.0,  0.0},
							 { 0.0, -1.0, 0.0},{0.0, 0.0, 1.0},{0.0, 0.0, -1.0}};
	static int faces[6][4] ={{0, 1, 2, 3},{3, 2, 6, 7},{7, 6, 5, 4},
							 {4, 5, 1, 0},{5, 6, 2, 1},{7, 4, 0, 3}};
	Vector3 v[8];
	v[0][0] = v[1][0] = v[2][0] = v[3][0] = -length / 2;
	v[4][0] = v[5][0] = v[6][0] = v[7][0] =  length / 2;
	v[0][1] = v[1][1] = v[4][1] = v[5][1] = -length / 2;
	v[2][1] = v[3][1] = v[6][1] = v[7][1] =  length / 2;
	v[0][2] = v[3][2] = v[4][2] = v[7][2] = -length / 2;
	v[1][2] = v[2][2] = v[5][2] = v[6][2] =  length / 2;

	std::vector<Vector3> quad;
	for(int i = 0; i < 6; i++){
		if( n[i][0] == direction[0] && n[i][1] == direction[1] && n[i][2] == direction[2] ){
			for(int vi = 0; vi < 4; vi++) quad.push_back( v[ faces[i][vi] ] );
			break;
		}
	}
	return quad;
}

inline std::vector< std::vector<uint64_t> > voxelPath(Eigen::Vector3i center, Eigen::Vector3i corner, size_t gridsize){
	std::vector< std::vector<uint64_t> > paths(6);

	bool isRight = corner.x() > center.x();
	bool isFront = corner.y() > center.y();
	bool isTop = corner.z() > center.z();

	if( center.z() == corner.z() ){
		paths.resize(2);
		paths[0].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z()) );
		paths[1].push_back( mortonEncode_LUT(corner.x(), corner.y() + (isFront ? -1 : 1), corner.z()) );
	}else if( center.x() == corner.x() ){
		paths.resize(2);
		paths[0].push_back( mortonEncode_LUT(corner.x(), corner.y() + (isFront ? -1 : 1), corner.z()) );
		paths[1].push_back( mortonEncode_LUT(corner.x(), corner.y(), corner.z() + (isTop ? -1 : 1)) );
	}else if( center.y() == corner.y() ){
		paths.resize(2);
		paths[0].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z()) );
		paths[1].push_back( mortonEncode_LUT(corner.x(), corner.y(), corner.z() + (isTop ? -1 : 1)) );
	}else{
		paths[0].push_back( mortonEncode_LUT(corner.x(), corner.y() + (isFront ? -1 : 1), corner.z()) );
		paths[0].push_back( mortonEncode_LUT(corner.x(), corner.y() + (isFront ? -1 : 1), corner.z() + (isTop ? -1 : 1)) );
		paths[1].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z())  );
		paths[1].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z() + (isTop ? -1 : 1)) );
		paths[2].push_back( mortonEncode_LUT(corner.x(), corner.y(), corner.z() + (isTop ? -1 : 1)) );
		paths[2].push_back( mortonEncode_LUT(corner.x(), corner.y()	+ (isFront ? -1 : 1), corner.z() + (isTop ? -1 : 1)) );
		paths[3].push_back( mortonEncode_LUT(corner.x(), corner.y(), corner.z() + (isTop ? -1 : 1)) );
		paths[3].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z() + (isTop ? -1 : 1)) );
		paths[4].push_back( mortonEncode_LUT(corner.x(), corner.y() + (isFront ? -1 : 1), corner.z()) );
		paths[4].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y() + (isFront ? -1 : 1), corner.z()) );
		paths[5].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y(), corner.z()) );
		paths[5].push_back( mortonEncode_LUT(corner.x() + (isRight ? -1 : 1), corner.y() + (isFront ? -1 : 1), corner.z()) );
	}
	for(auto & path : paths){
		bool isValid = true;
		for(auto step : path) {
			unsigned int x,y,z;
			mortonDecode(step,x,y,z);
			if( x > gridsize-1 || y > gridsize-1 || z > gridsize-1 ) isValid = false;
		}
		if( !isValid ) path.clear();
	}
	return paths;
}

// Implementation of algorithm from http://research.michael-schwarz.com/publ/2010/vox/ (Schwarz & Seidel)
// Adapted for mortoncode -based subgrids

template<typename Vector3>
inline void voxelize_schwarz_method(SurfaceMeshModel * mesh, const uint64_t morton_start, const uint64_t morton_end, 
		const double unitlength, std::vector<char> & voxels, vector< VoxelData<Vector3> > &data, size_t &nfilled) 
{
	voxels.clear();
	voxels.resize(morton_end - morton_start, EMPTY_VOXEL);

	data.clear();
	data.reserve(50000);

	// compute partition min and max in grid coords
	AABox< Eigen::Matrix<unsigned int, 3, 1> > p_bbox_grid;
	mortonDecode(morton_start, p_bbox_grid.min[2], p_bbox_grid.min[1], p_bbox_grid.min[0]);
	mortonDecode(morton_end - 1, p_bbox_grid.max[2], p_bbox_grid.max[1], p_bbox_grid.max[0]);

	// COMMON PROPERTIES FOR ALL TRIANGLES
	double unit_div = 1.0 / unitlength;
	Vector3 delta_p(unitlength, unitlength, unitlength);

	Vector3VertexProperty points = mesh->vertex_coordinates();

	// voxelize every triangle
	for(auto f : mesh->faces())
	{
		BasicTriangle<Vector3> t;
		for(auto vi : mesh->vertices(f)) t.setPoint( points[vi].cast<Vector3::Scalar>() );

		// compute triangle bbox in world and grid
		AABox<Vector3> t_bbox_world = computeBoundingBox(t.v0, t.v1, t.v2);
		AABox<Eigen::Vector3i> t_bbox_grid;
		t_bbox_grid.min[0] = (int)(t_bbox_world.min[0] * unit_div);
		t_bbox_grid.min[1] = (int)(t_bbox_world.min[1] * unit_div);
		t_bbox_grid.min[2] = (int)(t_bbox_world.min[2] * unit_div);
		t_bbox_grid.max[0] = (int)(t_bbox_world.max[0] * unit_div);
		t_bbox_grid.max[1] = (int)(t_bbox_world.max[1] * unit_div);
		t_bbox_grid.max[2] = (int)(t_bbox_world.max[2] * unit_div);

		// clamp
		t_bbox_grid.min[0] = clampval<int>(t_bbox_grid.min[0], p_bbox_grid.min[0], p_bbox_grid.max[0]);
		t_bbox_grid.min[1] = clampval<int>(t_bbox_grid.min[1], p_bbox_grid.min[1], p_bbox_grid.max[1]);
		t_bbox_grid.min[2] = clampval<int>(t_bbox_grid.min[2], p_bbox_grid.min[2], p_bbox_grid.max[2]);
		t_bbox_grid.max[0] = clampval<int>(t_bbox_grid.max[0], p_bbox_grid.min[0], p_bbox_grid.max[0]);
		t_bbox_grid.max[1] = clampval<int>(t_bbox_grid.max[1], p_bbox_grid.min[1], p_bbox_grid.max[1]);
		t_bbox_grid.max[2] = clampval<int>(t_bbox_grid.max[2], p_bbox_grid.min[2], p_bbox_grid.max[2]);

		// COMMON PROPERTIES FOR THE TRIANGLE
		Vector3 e0 = t.v1 - t.v0;
		Vector3 e1 = t.v2 - t.v1;
		Vector3 e2 = t.v0 - t.v2;
		Vector3 to_normalize = (e0).cross(e1);
		Vector3 n = (to_normalize).normalized(); // triangle normal

		// PLANE TEST PROPERTIES
		Vector3 c = Vector3(0.0, 0.0, 0.0); // critical point
		if (n[X_] > 0) { c[X_] = unitlength; }
		if (n[Y_] > 0) { c[Y_] = unitlength; }
		if (n[Z_] > 0) { c[Z_] = unitlength; }
		double d1 = n.dot(c - t.v0);
		double d2 = n.dot(Vector3(delta_p - c) - t.v0);

		// PROJECTION TEST PROPERTIES
		// XY plane
		Eigen::Vector2d n_xy_e0 = Eigen::Vector2d(-1.0*e0[Y_], e0[X_]);
		Eigen::Vector2d n_xy_e1 = Eigen::Vector2d(-1.0*e1[Y_], e1[X_]);
		Eigen::Vector2d n_xy_e2 = Eigen::Vector2d(-1.0*e2[Y_], e2[X_]);
		if (n[Z_] < 0.0) {
			n_xy_e0 = -1.0 * n_xy_e0;
			n_xy_e1 = -1.0 * n_xy_e1;
			n_xy_e2 = -1.0 * n_xy_e2;
		}
		double d_xy_e0 = (-1.0 * (n_xy_e0.dot(Eigen::Vector2d(t.v0[X_], t.v0[Y_])))) + std::max(0.0, unitlength*n_xy_e0[0]) + std::max(0.0, unitlength*n_xy_e0[1]);
		double d_xy_e1 = (-1.0 * (n_xy_e1.dot(Eigen::Vector2d(t.v1[X_], t.v1[Y_])))) + std::max(0.0, unitlength*n_xy_e1[0]) + std::max(0.0, unitlength*n_xy_e1[1]);
		double d_xy_e2 = (-1.0 * (n_xy_e2.dot(Eigen::Vector2d(t.v2[X_], t.v2[Y_])))) + std::max(0.0, unitlength*n_xy_e2[0]) + std::max(0.0, unitlength*n_xy_e2[1]);
		// YZ plane
		Eigen::Vector2d n_yz_e0 = Eigen::Vector2d(-1.0*e0[Z_], e0[Y_]);
		Eigen::Vector2d n_yz_e1 = Eigen::Vector2d(-1.0*e1[Z_], e1[Y_]);
		Eigen::Vector2d n_yz_e2 = Eigen::Vector2d(-1.0*e2[Z_], e2[Y_]);
		if (n[X_] < 0.0) {
			n_yz_e0 = -1.0 * n_yz_e0;
			n_yz_e1 = -1.0 * n_yz_e1;
			n_yz_e2 = -1.0 * n_yz_e2;
		}
		double d_yz_e0 = (-1.0 * (n_yz_e0.dot(Eigen::Vector2d(t.v0[Y_], t.v0[Z_])))) + std::max(0.0, unitlength*n_yz_e0[0]) + std::max(0.0, unitlength*n_yz_e0[1]);
		double d_yz_e1 = (-1.0 * (n_yz_e1.dot(Eigen::Vector2d(t.v1[Y_], t.v1[Z_])))) + std::max(0.0, unitlength*n_yz_e1[0]) + std::max(0.0, unitlength*n_yz_e1[1]);
		double d_yz_e2 = (-1.0 * (n_yz_e2.dot(Eigen::Vector2d(t.v2[Y_], t.v2[Z_])))) + std::max(0.0, unitlength*n_yz_e2[0]) + std::max(0.0, unitlength*n_yz_e2[1]);
		// ZX plane
		Eigen::Vector2d n_zx_e0 = Eigen::Vector2d(-1.0*e0[X_], e0[Z_]);
		Eigen::Vector2d n_zx_e1 = Eigen::Vector2d(-1.0*e1[X_], e1[Z_]);
		Eigen::Vector2d n_zx_e2 = Eigen::Vector2d(-1.0*e2[X_], e2[Z_]);
		if (n[Y_] < 0.0) {
			n_zx_e0 = -1.0 * n_zx_e0;
			n_zx_e1 = -1.0 * n_zx_e1;
			n_zx_e2 = -1.0 * n_zx_e2;
		}
		double d_xz_e0 = (-1.0 * (n_zx_e0.dot(Eigen::Vector2d(t.v0[Z_], t.v0[X_])))) + std::max(0.0, unitlength*n_zx_e0[0]) + std::max(0.0, unitlength*n_zx_e0[1]);
		double d_xz_e1 = (-1.0 * (n_zx_e1.dot(Eigen::Vector2d(t.v1[Z_], t.v1[X_])))) + std::max(0.0, unitlength*n_zx_e1[0]) + std::max(0.0, unitlength*n_zx_e1[1]);
		double d_xz_e2 = (-1.0 * (n_zx_e2.dot(Eigen::Vector2d(t.v2[Z_], t.v2[X_])))) + std::max(0.0, unitlength*n_zx_e2[0]) + std::max(0.0, unitlength*n_zx_e2[1]);

		// test possible grid boxes for overlap
		for (int x = t_bbox_grid.min[0]; x <= t_bbox_grid.max[0]; x++){
			for (int y = t_bbox_grid.min[1]; y <= t_bbox_grid.max[1]; y++){
				for (int z = t_bbox_grid.min[2]; z <= t_bbox_grid.max[2]; z++){

					uint64_t index = mortonEncode_LUT(z, y, x);

					if (voxels[index - morton_start] == FULL_VOXEL){ continue; } // already marked, continue

					// TRIANGLE PLANE THROUGH BOX TEST
					Vector3 p = Vector3(x*unitlength, y*unitlength, z*unitlength);
					double nDOTp = n.dot(p);
					if ((nDOTp + d1) * (nDOTp + d2) > 0.0){ continue; }

					// PROJECTION TESTS
					// XY
					Eigen::Vector2d p_xy = Eigen::Vector2d(p[X_], p[Y_]);
					if ((n_xy_e0.dot(p_xy) + d_xy_e0) < 0.0){ continue; }
					if ((n_xy_e1.dot(p_xy) + d_xy_e1) < 0.0){ continue; }
					if ((n_xy_e2.dot(p_xy) + d_xy_e2) < 0.0){ continue; }

					// YZ
					Eigen::Vector2d p_yz = Eigen::Vector2d(p[Y_], p[Z_]);
					if ((n_yz_e0.dot(p_yz) + d_yz_e0) < 0.0){ continue; }
					if ((n_yz_e1.dot(p_yz) + d_yz_e1) < 0.0){ continue; }
					if ((n_yz_e2.dot(p_yz) + d_yz_e2) < 0.0){ continue; }

					// XZ	
					Eigen::Vector2d p_zx = Eigen::Vector2d(p[Z_], p[X_]);
					if ((n_zx_e0.dot(p_zx) + d_xz_e0) < 0.0){ continue; }
					if ((n_zx_e1.dot(p_zx) + d_xz_e1) < 0.0){ continue; }
					if ((n_zx_e2.dot(p_zx) + d_xz_e2) < 0.0){ continue; }

					voxels[index - morton_start] = FULL_VOXEL;
					data.push_back(VoxelData<Vector3>(index, true, n));

					nfilled++;
					continue;
				}
			}
		}
	}
}

template<typename Vector3>
inline AABox<Vector3> createMeshBBCube( SurfaceMeshModel * mesh )
{
	Eigen::AlignedBox3d mesh_bbox = mesh->bbox();

	// Numerical stability : but why we need this?
	mesh_bbox = mesh_bbox.extend( ((mesh_bbox.max() - mesh_bbox.center()) * (1 + 1e-12)) + mesh_bbox.center() );

	Vector3 mesh_min = mesh_bbox.min().cast<Vector3::Scalar>();
	Vector3 mesh_max = mesh_bbox.max().cast<Vector3::Scalar>();

	Vector3 lengths = mesh_max - mesh_min;

	for(int i=0; i<3;i++){
		double delta = lengths.maxCoeff() - lengths[i];
		if(delta != 0){
			mesh_min[i] = mesh_min[i] - (delta / 2.0);
			mesh_max[i] = mesh_max[i] + (delta / 2.0);
		}
	}
	return AABox<Vector3>(mesh_min, mesh_max);
}

template<typename Vector3>
inline VoxelContainer<Vector3> ComputeVoxelization( SurfaceMeshModel * mesh, size_t gridsize, 
												   bool isMakeSolid, bool isManifoldReady, bool isInsideUniteCube = false )
{
	VoxelContainer<Vector3> container;

	// Move mesh to positive world
	Vector3VertexProperty points = mesh->vertex_coordinates();
	mesh->updateBoundingBox();
	Vector3 corner(0,0,0), delta(0,0,0);

	if( !isInsideUniteCube )
	{
		corner = mesh->bbox().min().cast<Vector3::Scalar>();
		delta = mesh->bbox().center().cast<Vector3::Scalar>() - corner;
		for(auto v : mesh->vertices()) points[v] -= corner.cast<double>();

		AABox<Vector3> mesh_bbox = createMeshBBCube<Vector3>( mesh );
		container.unitlength = (mesh_bbox.max[0] - mesh_bbox.min[0]) / (float)gridsize;
	}
	else
	{
		container.unitlength = 1.0 / gridsize;
	}

	container.gridsize = gridsize;
	uint64_t morton_part = (gridsize * gridsize * gridsize);

	// Storage for voxel on/off
	std::vector<char> voxels; 

	// morton codes for this partition
	uint64_t start = 0;
	uint64_t end = morton_part;
	size_t nfilled = 0;

	// VOXELIZATION
	voxelize_schwarz_method(mesh, start, end, container.unitlength, voxels, container.data, nfilled);

	container.translation = corner;

	// Move mesh back to original position
	for(auto v : mesh->vertices()) points[v] += corner.cast<double>();

	// Voxels on surface are marked as so
	for(auto & v : container.data) v.isOuter = true;

	container.isSolid = isMakeSolid;

	if( isMakeSolid )
	{	
		// Original set of surface voxels (sparse representation)
		std::set<uint64_t> surface_voxels;
		for(uint64_t m = 0; m < morton_part; m++) if(voxels.at(m) == FULL_VOXEL) surface_voxels.insert(m);

		// Flood fill from the outside walls
		std::vector<uint64_t> outer;
		for(int u = 0; u < gridsize; u++){
			for(int v = 0; v < gridsize; v++){
				outer.push_back( mortonEncode_LUT(u, v, 0) );
				outer.push_back( mortonEncode_LUT(0, u, v) );
				outer.push_back( mortonEncode_LUT(v, 0, u) );

				outer.push_back( mortonEncode_LUT(u, v, (unsigned int)gridsize-1) );
				outer.push_back( mortonEncode_LUT((unsigned int)gridsize-1, u, v) );
				outer.push_back( mortonEncode_LUT(v, (unsigned int)gridsize-1, u) );
			}
		}

		// Split into 8 chunks
		int h = (int)gridsize / 2; 
		std::vector<Eigen::AlignedBox3i> chunk( 8 );
		chunk[0] = Eigen::AlignedBox3i( Eigen::Vector3i(0,0,0), Eigen::Vector3i(h-1,h-1,h-1) ); // Top
		chunk[1] = Eigen::AlignedBox3i( chunk[0].min() + Eigen::Vector3i(h,0,0), chunk[0].max() + Eigen::Vector3i(h,0,0) );
		chunk[2] = Eigen::AlignedBox3i( chunk[1].min() + Eigen::Vector3i(0,h,0), chunk[1].max() + Eigen::Vector3i(0,h,0) );
		chunk[3] = Eigen::AlignedBox3i( chunk[2].min() - Eigen::Vector3i(h,0,0), chunk[2].max() - Eigen::Vector3i(h,0,0) );
		chunk[4] = Eigen::AlignedBox3i( chunk[0].min() + Eigen::Vector3i(0,0,h), chunk[0].max() + Eigen::Vector3i(0,0,h) );	// Bottom
		chunk[5] = Eigen::AlignedBox3i( chunk[1].min() + Eigen::Vector3i(0,0,h), chunk[1].max() + Eigen::Vector3i(0,0,h) );
		chunk[6] = Eigen::AlignedBox3i( chunk[2].min() + Eigen::Vector3i(0,0,h), chunk[2].max() + Eigen::Vector3i(0,0,h) );
		chunk[7] = Eigen::AlignedBox3i( chunk[3].min() + Eigen::Vector3i(0,0,h), chunk[3].max() + Eigen::Vector3i(0,0,h) );

		// Assign wall voxels to their chunks
		std::vector< std::deque<uint64_t> > wall( chunk.size() );
		for(auto m : outer)
		{
			unsigned int x,y,z;
			mortonDecode(m, x, y, z);
			for(size_t i = 0; i < chunk.size(); i++){
				if( chunk[i].contains( Eigen::Vector3i(x,y,z) ) ){
					wall[i].push_back(m); break;
				}
			}
		}

		// Do parallel flood-fill
		#pragma omp parallel for
		for(int i = 0; i < (int)wall.size(); i++)
		{
			std::deque<uint64_t> & queue = wall[i];

			// Flood fill (do we need to visit diagonals as well??)
			while( !queue.empty() )
			{
				uint64_t curVox = queue.front();
				queue.pop_front();

				const uint64_t vox = voxels.at(curVox);

				if( vox == EMPTY_VOXEL )
				{
					voxels.at(curVox) = FULL_VOXEL;

					unsigned int x,y,z;
					mortonDecode(curVox, x, y, z);

					if(x < gridsize - 1) queue.push_back( mortonEncode_LUT(x+1,y,z) );
					if(y < gridsize - 1) queue.push_back( mortonEncode_LUT(x,y+1,z) );
					if(z < gridsize - 1) queue.push_back( mortonEncode_LUT(x,y,z+1) );

					if(x > 0) queue.push_back( mortonEncode_LUT(x-1,y,z) );
					if(y > 0) queue.push_back( mortonEncode_LUT(x,y-1,z) );
					if(z > 0) queue.push_back( mortonEncode_LUT(x,y,z-1) );
				}
			}
		}

		// Now carve out surface
		for(auto s : surface_voxels) voxels[s] = EMPTY_VOXEL;

		// Solid voxels to surface
		if( isManifoldReady )
		{
			// First ensure voxels result in watertight surface
			bool isFixing = true;
			while( isFixing )
			{
				isFixing = false;

				// Test six sides of a voxel on surface
				for(auto s : surface_voxels)
				{
					unsigned int x,y,z;
					mortonDecode(s, x, y, z);

					// Examine my neighborhood
					for(int u = -1; u <= 1; u++){
						for(int v = -1; v <= 1; v++){
							for(int w = -1; w <= 1; w++){
								if(u == 0 && v == 0 && w == 0) continue; // skip myself

								Eigen::Vector3i c(x + u, y + v, z + w);

								// Skip outside grid
								if(c.x() < 0 || c.y() < 0 || c.z() < 0) continue;
								if(c.x() > gridsize-1 || c.y() > gridsize-1 || c.z() > gridsize-1) continue;;

								if( voxels[mortonEncode_LUT( c.x(), c.y(), c.z() )] != EMPTY_VOXEL ) continue;

								// Get possible paths from voxel to current corner
								auto paths = voxelPath( Eigen::Vector3i(x,y,z), c, gridsize );
								int filled = 0;
								for(auto path : paths){
									int dist = 0;
									for(auto step : path) dist += (voxels[step] == EMPTY_VOXEL) ? 1 : 0;
									if(dist == path.size()) filled++;
								}

								// Expand when no path connects the corner and current voxel
								if( !filled ){
									for(auto path : paths){
										for(auto step : path){
											isFixing = true;
											voxels[step] = EMPTY_VOXEL;
											surface_voxels.insert( step );
											//if(false) container.aux.push_back( VoxelData<Vector3>(step, true) ); // DEBUG
										}
									}
								}

								// Unfilled diagonal case
								if( paths.size() > 2 && filled == 2 ){
									std::vector<uint64_t> d(4);
									d[0] = mortonEncode_LUT(x, y + v, z + w); d[1] = mortonEncode_LUT(x + u, y, z);
									d[2] = mortonEncode_LUT(x + u, y, z + w); d[3] = mortonEncode_LUT(x, y + v, z);

									if( (voxels[d[0]] != EMPTY_VOXEL && voxels[d[1]] != EMPTY_VOXEL) || 
										(voxels[d[2]] != EMPTY_VOXEL && voxels[d[3]] != EMPTY_VOXEL) ){
										for(int i = 0; i < 2; i++){
											isFixing = true;
											voxels[d[i]] = EMPTY_VOXEL;
											surface_voxels.insert( d[i] );
											//if(false) container.aux.push_back( VoxelData<Vector3>(d[i], true) ); // DEBUG
										}
									}
								}
							}
						}
					}
				}
			}

			// Collect inner voxels
			container.data.clear(); // remove surface voxels
			for(uint64_t m = 0; m < morton_part; m++){
				if(voxels[m] == EMPTY_VOXEL){
					container.data.push_back( VoxelData<Vector3>(m, (surface_voxels.find(m) != surface_voxels.end())) );
				}
			}

			// Collect set of pair voxels (inside / outside)
			std::vector< std::pair<uint64_t,uint64_t> > surface;
			for(auto voxel : container.data)
			{					
				unsigned int x,y,z;
				mortonDecode(voxel.morton, x, y, z);

				std::vector<uint64_t> neigh;
				if(x < gridsize - 1) neigh.push_back( mortonEncode_LUT(x+1,y,z) );
				if(y < gridsize - 1) neigh.push_back( mortonEncode_LUT(x,y+1,z) );
				if(z < gridsize - 1) neigh.push_back( mortonEncode_LUT(x,y,z+1) );

				if(x > 0) neigh.push_back( mortonEncode_LUT(x-1,y,z) );
				if(y > 0) neigh.push_back( mortonEncode_LUT(x,y-1,z) );
				if(z > 0) neigh.push_back( mortonEncode_LUT(x,y,z-1) );

				// Collect pair of voxels at surface
				for(auto n : neigh){
					if(voxels[n] != voxels[voxel.morton])
						surface.push_back( std::make_pair(voxel.morton, n) );
				}
			}

			std::vector< std::pair<uint64_t, Eigen::Vector3i> > allQuads;

			// Also similarly add quads from boundary voxels
			for(auto s : surface_voxels){
				unsigned int v[3];
				mortonDecode(s, v[2], v[1], v[0]);
				bool isBoundary = (v[0] == 0 || v[1] == 0 || v[2] == 0) || 
					(v[0] == gridsize-1 || v[1] == gridsize-1 || v[2] == gridsize-1);
				if( isBoundary ){
					for(int i = 0; i < 3; i++){
						Eigen::Vector3i d(0,0,0);
						if(v[i] == 0) d[i] = -1;
						else if(v[i] == gridsize-1) d[i] = 1;
						if(d[0]!=0||d[1]!=0||d[2]!=0) 
							allQuads.push_back( std::make_pair(s, d) );
					}
				}
			}

			// Prepare set of quads
			for(auto s : surface){
				unsigned int v[3], w[3];
				mortonDecode(s.first, v[0], v[1], v[2]);
				mortonDecode(s.second, w[0], w[1], w[2]);
				Eigen::Vector3i direction (int(w[2])-int(v[2]), int(w[1])-int(v[1]), int(w[0])-int(v[0]));
				allQuads.push_back( std::make_pair(s.first, direction) );
			}

			// Generate surface quads in world coordinates
			double unitlength = container.unitlength;	
			Vector3 delta = container.translation + ( 0.5 * Vector3(unitlength,unitlength,unitlength) );

			for(auto p : allQuads)
			{			
				unsigned int v[3];
				mortonDecode(p.first, v[0], v[1], v[2]);
				std::vector<Vector3> quad = voxelQuad<Vector3>( p.second, unitlength );
				for(auto & p : quad) p += Vector3(v[2] * unitlength, v[1] * unitlength, v[0] * unitlength) + delta;
				container.quads.push_back( quad );
			}
		}
		else
		{
			// Just collect voxels
			container.data.clear();
			for(uint64_t m = 0; m < morton_part; m++){
				if(voxels[m] == EMPTY_VOXEL)
					container.data.push_back( VoxelData<Vector3>(m, (surface_voxels.find(m) != surface_voxels.end())) );
			}
		}
	}

	return container;
}

#include "NanoKdTree.h"
inline void snapCloseVertices( std::vector<SurfaceMesh::Vector3> & vertices, double threshold ){
	NanoKdTree tree;
	for(auto p : vertices) tree.addPoint(p);
	tree.build();
	for(auto & p : vertices){
		KDResults matches;
		tree.ball_search(p, threshold, matches);
		for(auto m : matches) vertices[m.first] = p;
	}
} 

inline bool CompareVector3(const Vector3& p, const Vector3& q){ 
	if(p.x() == q.x()){
		if(p.y() == q.y()) return p.z() < q.z();	
		return p.y() < q.y();
	}
	return p.x() < q.x();
}

inline void meregeVertices(SurfaceMesh::SurfaceMeshModel * m)
{
	// Collect original vertices
	std::vector<SurfaceMesh::Vector3> original;
	SurfaceMesh::Vector3VertexProperty points = m->vertex_coordinates();
	for( auto v : m->vertices() ) original.push_back( points[v] );

	// Average edge length
	double avgEdge = 0;
	for(auto e : m->edges()) avgEdge += ( points[ m->vertex(e,0) ] - points[ m->vertex(e,1) ] ).norm();
	avgEdge /= m->n_edges();

	double closeThreshold = avgEdge * 0.01;

	// Snap close vertices, sort them, then remove duplicates
	snapCloseVertices( original, closeThreshold );	
	std::vector<SurfaceMesh::Vector3> clean = original;
	std::sort( clean.begin(), clean.end(), CompareVector3);
	clean.erase( std::unique(clean.begin(), clean.end()), clean.end() );

	// Find new ids
	std::vector<size_t> xrefs( original.size(), 0 );
	for (size_t i = 0; i != original.size(); i += 1)
		xrefs[i] = std::lower_bound(clean.begin(), clean.end(), original[i], CompareVector3) - clean.begin();

	// Replace face vertices
	std::vector< std::vector<SurfaceMesh::Vertex> > faces;
	for(auto f: m->faces()){
		std::vector<SurfaceMesh::Vertex> faceverts;
		for(auto v: m->vertices(f)) faceverts.push_back( SurfaceMesh::Vertex((int)xrefs[ v.idx() ]) );
		faceverts.erase( std::unique(faceverts.begin(), faceverts.end()), faceverts.end() );
		if(faceverts.size() == 3) faces.push_back(faceverts); // skip degenerate faces
	}

	// Rebuild
	m->clear();
	for(auto v: clean) m->add_vertex(v);
	for(auto face: faces) m->add_face(face);
}