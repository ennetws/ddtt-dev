#include "Segmentation.h"

#include "myglobals.h"
#include "Planes.h"
#include "Bounds.h"
#include "graph_helper.h"

#include "disjointset.h"

#include "SplitOperation.h"

Segmentation::Segmentation(ParticleMesh *fromMesh) : s(fromMesh)
{
    std::random_shuffle(ParticleMesh::rndcolors.begin(), ParticleMesh::rndcolors.end());

    rc = 0; // reset random color
    globalDebug.clear();

    SplitOperation op( s, s->toGraph() );
    op.split();

    std::vector<SplitOperation*> clusters;
    op.collectClusters( clusters );

    std::map<size_t,size_t> mappedClusters;
    for(auto c : clusters)
        mappedClusters[c->seg.uid] = mappedClusters.size();

    // Assign new clusters
    for(auto c : clusters)
        for(auto v : c->seg.vertices)
            s->particles[v].segment = (int)mappedClusters[c->seg.uid];

    // Merging
    if( s->property["isMerge"].toBool() )
    {
        mergeSimilar();
        mergeConvex();

        // Final cleanup
        {
            s->shrinkSmallerClusters();

            // Reassign smaller segments
            int small_segment_threshold = s->grid.gridsize * 0.15;

            bool isDone = false;
            while(!isDone){
                isDone = true;

                SegmentGraph neiGraph;
                auto candidates = s->segmentToComponents( s->toGraph(), neiGraph );

                for(auto & smallSeg : candidates){
                    if(smallSeg.vertices.size() > small_segment_threshold) continue;

					auto Msmall = toEigenMatrix<double>(s->particlesPositions(smallSeg.vertices));
                    Vector3 smallCenter = Msmall.colwise().mean();
					
                    std::map<size_t,Vector3> candiateCenters;
                    for(auto largeID : neiGraph.GetNeighbours((unsigned int)smallSeg.uid)){
                        auto & largeSeg = candidates[largeID];
                        if(largeSeg.vertices.size() <= small_segment_threshold) continue;

						auto Mlarge = toEigenMatrix<double>(s->particlesPositions(largeSeg.vertices));
                        candiateCenters[largeID] = Mlarge.colwise().mean();
                    }

                    if(candiateCenters.size() < 1) continue;

                    // Reassign
                    for(auto v : smallSeg.vertices){
                        QMap<double,size_t> distSeg;
                        for(auto segID_center : candiateCenters){
                            auto & largeSeg = candidates[segID_center.first];
                            distSeg[(candiateCenters[largeSeg.uid]-smallCenter).norm()] = largeSeg.sid;
                        }

                        // Closest segment is best
                        int bestSeg = distSeg.values().front();
                        s->particles[v].segment = bestSeg;
                    }

                    isDone = false;
                }
            }
        }
    }

    // DEBUG: Draw hull around segments
    if( s->property["showHulls"].toBool() )
    {
        SegmentGraph neiGraph;
        auto candidates = s->segmentToComponents( s->toGraph(), neiGraph );

        int sid = 0;

        for(auto & seg : candidates)
        {
            starlab::PolygonSoup * ps = new starlab::PolygonSoup;
            for( auto f : ConvexHull<Vector3>( s->particlesCorners(seg.vertices) ).faces )
            {
                QVector<starlab::QVector3> face;
                for(auto v : f) face << v;
                ps->addPoly(face, ParticleMesh::rndcolors[sid]);
            }

            sid++;
            debug << ps;
        }
    }

    for(auto d : globalDebug) debug << d;
}

void Segmentation::mergeConvex()
{
    bool isDone = false;

    while( !isDone )
    {
        isDone = true;

        // Get candidate good segments:
        SegmentGraph neiGraph;
        auto candidates = s->segmentToComponents( s->toGraph(), neiGraph );

        // Pre-compute convex hulls:
        QMap< size_t, ConvexHull<Vector3> > hulls;
        for(auto & seg : candidates)
            hulls[seg.uid] = ConvexHull<Vector3>( s->particlesCorners(seg.vertices) );

        // Merge smaller clusters
        bool isMerge = true;
        while( isMerge )
        {
            isMerge = false;

            // Sort candidates by their size
            std::vector<SegmentGraph*> sorted;
            for(auto c : candidates.keys()) if(candidates[c].vertices.size()) sorted.push_back( &candidates[c] );
            std::sort( sorted.begin(), sorted.end(), [&](const SegmentGraph* a, const SegmentGraph* b){
                return a->vertices.size() < b->vertices.size();
            });

            std::vector< std::pair<double, std::pair<size_t,size_t> > > possibleMerges;

            // Test if small cluster merges to a better solidity score
            for(auto seg : sorted)
            {
                auto & hull = hulls[seg->uid];
                std::map<int, ConvexHull<Vector3> > newHulls;
                int bestJ = (int)seg->uid;
                double bestScore = 0.6;

                //double mysol = hull.solidity(s->grid.unitlength);
                //if(mysol > 0.9) bestScore = mysol;

                for(auto j : neiGraph.getEdges((int)seg->uid))
                {
                    if(candidates[j].vertices.empty()) continue;

                    auto newHull = hull.merged(hulls[j]);
                    double solidity = newHull.solidity(s->grid.unitlength);

                    if(solidity > bestScore){
                        bestScore = solidity;
                        bestJ = j;
                        newHulls[j] = newHull;
                    }
                }

                if(bestJ == seg->uid) continue; // no merge is good

                auto big = &candidates[(unsigned int)seg->uid];
                auto smaller = &candidates[bestJ];
                if(big->vertices.size() < smaller->vertices.size()) std::swap(big,smaller);

                // Check if descriptor wise they are different
                auto & pi = s->particles[s->closestParticles( hulls[seg->uid].center ).front().second];
                auto & pj = s->particles[s->closestParticles( hulls[bestJ].center ).front().second];
                double similarity = 1 - abs(pi.flat - pj.flat);
                if(similarity < 0.4) break;

                // Migrate from small to big
                for(auto v : smaller->vertices)
                    s->particles[v].segment = (int)big->sid;

                smaller->vertices.clear();

                hulls[big->uid] = newHulls[bestJ];

                isMerge = true;
                isDone = false;

                break;
            }
        }

        // Assign final segment IDs
        int sid = 0;
        for(auto & seg : candidates)
        {
            for(auto v : seg.vertices)
                s->particles[v].segment = sid;
            sid++;
        }
    }
}

void Segmentation::mergeSimilar()
{
    SegmentGraph neiGraph;
    auto candidates = s->segmentToComponents( s->toGraph(), neiGraph );
    QMap<size_t,size_t> segMap, mapSeg;
    QMap<size_t,Vector3> segDirection;

    for(auto & seg : candidates)
    {
        mapSeg[segMap.size()] = seg.uid;
        segMap[seg.uid] = segMap.size();

        std::vector<Vector3> pnts;
        for(auto v : seg.vertices) pnts.push_back(s->particles[v].pos);
        auto mat = toEigenMatrix<double>(pnts);

        // PCA
        Eigen::MatrixXd centered = mat.rowwise() - mat.colwise().mean();
        Eigen::MatrixXd cov = centered.adjoint() * centered;
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(cov);

        // Largest
        auto dir = eig.eigenvectors().col(2);

        // DEBUG:
        //auto vs = new starlab::VectorSoup;
        //vs->addVector(s->particles[rep].pos, Vector3(dir * 0.2));
        //debug << vs;

        segDirection[seg.uid] = dir;
    }

    DisjointSet disjoint(candidates.size());

    double similarity_threshold = 0.92;

    for(auto segID : neiGraph.vertices){
        // Ignore bad candidates
        auto hull = ConvexHull<Vector3>( s->particlesCorners(candidates[segID].vertices) );
        if(hull.solidity(s->grid.unitlength) < 0.35) continue;

        for(auto neiID : neiGraph.GetNeighbours(segID)){
            double similarity = abs( segDirection[segID].dot(segDirection[neiID]) );
            if(similarity > similarity_threshold)
            {
                disjoint.Union((int)segMap[segID], (int)segMap[neiID]);
            }
        }
    }

    for(size_t i = 0; i < candidates.size(); i++)
    {
        auto segID = mapSeg[i];
        for(auto v: candidates[(unsigned int)segID].vertices)
            s->particles[v].segment = disjoint.Parent[i];
    }
}
