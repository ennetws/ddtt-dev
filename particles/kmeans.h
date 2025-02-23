// Code from Mathias Eitz and Ronald Richter

#pragma once

#include <vector>
#include <algorithm>
#include <iostream>
#include <set>
#include <random>
#include <functional>
#include <omp.h>

#include <QDebug>

namespace clustering{

enum KmeansInitAlgorithm
{
    KmeansInitRandom,
    KmeansInitPlusPlus,
	KmeansInitSpecial
};

template <class index_t, class collection_t>
void kmeans_init_random(std::vector<index_t>& centers, const collection_t& collection, std::size_t numclusters)
{
    assert(collection.size() >= numclusters);

    centers.resize(collection.size());
    for (std::size_t i = 0; i < centers.size(); i++) 
		centers[i] = i;
    std::random_shuffle(centers.begin(), centers.end());
    centers.resize(numclusters);
}

template <class index_t, class collection_t, class dist_fn>
void kmeans_init_plusplus(std::vector<index_t>& result, const collection_t& collection, std::size_t numclusters, const dist_fn& distfn)
{
    assert(numclusters > 0);
    assert(collection.size() >= numclusters);

    typedef typename collection_t::value_type item_t;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    std::size_t numtrials = 2 + std::log(numclusters);

    // add first cluster, randomly chosen
    std::set<index_t> centers;
    index_t first = dis(gen) * collection.size();
    centers.insert(first);

    // compute distance between first cluster center and all others
    // and accumulate the distances that gives the current potential
    std::vector<double> dists(collection.size());
    double potential = 0.0;
    for (std::size_t i = 0; i < collection.size(); i++)
    {
        double d = distfn(collection[first], collection[i]);
        dists[i] = d*d;
        potential += dists[i];
    }
    std::cout << "kmeans++ init: numclusters=" << numclusters << " numtrials=" << numtrials << " collection.size=" << collection.size() << " init pot=" << potential << std::endl;

    // iteratively add centers
    for (std::size_t c = 1; c < numclusters; c++)
    {
        double min_potential = std::numeric_limits<double>::max();
        std::size_t best_index = 0;

        for (std::size_t i = 0; i < numtrials; i++)
        {
            std::size_t index;

            // get new center
            double r = dis(gen) * potential;
            for (index = 0; index < collection.size()-1 && r > dists[index]; index++)
            {
                r -= dists[index];
            }

            while (centers.count(index) > 0) index = (index + 1) % collection.size();

            // recompute potential
            double p = 0.0;
            for (std::size_t k = 0; k < collection.size(); k++)
            {
                double d = distfn(collection[index], collection[k]);
                p += std::min(dists[k], d*d);
            }

            if (p < min_potential)
            {
                min_potential = p;
                best_index = index;
            }
        }

		#pragma omp parallel for
        for (int i = 0; i < (int)collection.size(); i++)
        {
            double d = distfn(collection[best_index], collection[i]);
            dists[i] = d*d;
        }

        potential = min_potential;

        centers.insert(best_index);

        std::cout << "new center " << c << ": potential=" << potential << " index=" << best_index << std::endl;
    }

    std::copy(centers.begin(), centers.end(), std::back_inserter(result));
}

/**
 * @brief Standard kmeans clustering
 */
template <class collection_t, class dist_fn>
class kmeans
{
    typedef typename collection_t::value_type sample_t;
	typedef typename sample_t::value_type scalar_t;

    public:

    /**
     * @brief Standard k-means clustering given a distance function
     * @param collection Data structure containing the samples to be clustered, typically a vector<vector<float> >, where the 'inner'
     * vector<float> would be a single sample.
     * @param numclusters Number of clusters to use.
     * @param initalgorithm Algorithm used to estimate the initial cluster centers
     * @param distfn Distance function used for comparing two samples.
     */
    kmeans(const collection_t& collection, std::size_t numclusters, KmeansInitAlgorithm initalgorithm = KmeansInitRandom, const dist_fn& distfn = dist_fn() )
     : _collection(collection), _distfn(distfn), _centers(numclusters), _clusters(collection.size())
    {
        if (initalgorithm == KmeansInitPlusPlus)
        {
            kmeans_init_plusplus(initindices, collection, numclusters, distfn);
        }
        else if(initalgorithm == KmeansInitRandom)
        {
            kmeans_init_random(initindices, collection, numclusters);
        }

		for (std::size_t i = 0; i < initindices.size(); i++) 
			_centers[i] = collection[initindices[i]];
    }

	// get initial centers
	std::vector<std::size_t> initindices;

    /**
     * @brief Perform k-means clustering on the dataset provided in the constructor
     *
     * Iterates until at least one of the following two criteria are met:
     * - maximum number of iterations reached
     * - the fraction of samples that changed clusters is below minchangesfraction
     * @param maxiteration Maximum number of iterations
     * @param minchangesfraction Fraction of changes, if less changes happen in a certain iteration, clustering is done.
     */
	void run(std::size_t maxiteration = 1000, double minchangesfraction = 0.005)
	{
		// main iteration
		std::size_t iteration = 0;
		for (;;)
		{
			if (maxiteration > 0 && iteration == maxiteration) break;

			QTime time;
			time.start();
			std::size_t changes = 0;

			// distribute items on clusters in parallel
			std::size_t idx = 0;
			distribute_samples(idx, changes);

			iteration++;

			std::cout << "changes: " << changes << " distribution time: " << time.elapsed() << std::endl;
			qDebug() << QString("changes: %1 distribution time: %2").arg(changes).arg(time.elapsed());

			if (changes <= std::ceil(_collection.size() * minchangesfraction)) break;

			// compute new centers
			std::vector<std::size_t> clustersize(_centers.size(), 0);
			for (std::size_t i = 0; i < _collection.size(); i++)
			{
				std::size_t k = _clusters[i];

				// if it is the first assignment for that cluster, then zero the center
				if (clustersize[k] == 0) std::fill(_centers[k].begin(), _centers[k].end(), scalar_t(0));

				add_operation(_centers[k], _collection[i]);
				clustersize[k]++;
			}

			// assign new centers
			std::vector<std::size_t> invalid, valid;
			for (std::size_t i = 0; i < _centers.size(); i++)
			{
				if (clustersize[i] > 0)
				{
					div_operation(_centers[i], clustersize[i]);
					valid.push_back(i);
				}
				else
				{
					invalid.push_back(i);
				}
				//std::cout << "clustersize " << i << ": " << clustersize[i] << std::endl;
			}

			// fix invalid centers, i.e. those with no members
			while (!invalid.empty() && !valid.empty())
			{
				std::size_t current = invalid.back();
				std::cout << "handle invalid clusters: " << invalid.size() << std::endl;
				// compute for each valid cluster the variance
				// of distances to all members and get the
				// most distant member
				std::vector<double> maxdist(_centers.size(), 0.0);
				std::vector<double> variance(_centers.size(), 0.0);
				std::vector<std::size_t> farthest(_centers.size());
				for (std::size_t i = 0; i < valid.size(); i++)
				{
					std::size_t c = valid[i];

					for (std::size_t k = 0; k < _collection.size(); k++)
					{
						if (_clusters[k] != c) continue;

						double d = _distfn(_collection[k], _centers[c]);
						if (d > maxdist[c])
						{
							maxdist[c] = d;
							farthest[c] = k;
						}
						variance[c] += d*d;
					}
					variance[c] /= clustersize[c];
				}

				// get cluster with highest variance and make
				// the farthest member of that cluster the
				// new center
				std::size_t c = std::distance(variance.begin(), std::max_element(variance.begin(), variance.end()));
				_centers[current] = _collection[farthest[c]];
				_clusters[farthest[c]] = current;

				std::cout << "reassign " << current << " to sample " << farthest[c] << " of cluster " << c << std::endl;

				valid.pop_back();
				invalid.pop_back();
			}

			std::cout << "iteration " << iteration << " time: " << time.elapsed() << std::endl;
			qDebug() << QString("iteration %1 time: ").arg(iteration).arg(time.elapsed());
		}

		std::cout << "kmeans iterations: " << iteration << std::endl;	
	}

    /// Vector of cluster membership: clusters[i] = j means that the sample with index i
    /// is a member of cluster j
    const std::vector<std::size_t>& clusters() const
    {
        return _clusters;
    }

	const std::size_t& cluster(size_t i) const
	{
		return _clusters[i];
	}

    /// Vector of cluster centers
    const std::vector<sample_t>& centers() const
    {
        return _centers;
    }

    template <class T>
    static void add_operation(T& lhs, const T& rhs)
    {
        for (int i = 0; i < (int)lhs.size(); i++) 
			lhs[i] += rhs[i];
    }

    template <class T>
    static void div_operation(T& lhs, double rhs)
	{
        for (int i = 0; i < (int)lhs.size(); i++) 
			lhs[i] /= rhs;
    }

    private:

    void distribute_samples(std::size_t& index, std::size_t& changes)
    {
		std::vector<double> dists(_centers.size());
		std::size_t currentchanges = 0;

		for (; index < _collection.size(); index++ )
		{
			#pragma omp parallel for
			for(int ci = 0; ci < (int)_centers.size(); ci++)
				dists[ci] = _distfn(_centers[ci], _collection[index]);

			// find the minimum distance, i.e. the nearest center
			std::size_t c = std::distance(dists.begin(), std::min_element(dists.begin(), dists.end()));

			// update cluster membership
			if (_clusters[index] != c)
			{
				_clusters[index] = c;
				currentchanges++;
			}
		}

		changes += currentchanges;
    }

    const collection_t& _collection;
    const dist_fn&      _distfn;

	std::vector<std::size_t> _clusters;
public:
	std::vector<sample_t>    _centers;
};


/**
 * @brief Squared L2 (Euclidean) distance function.
 *
 * Using the squared Euclidean distance is a bit faster than the L2 norm as we avoid the call to sqrt.
 */
template <class T>
struct l2norm_squared{
	typedef typename T::value_type R;

    /// Squared L2 distance between a and b.
    R operator() (const T& a, const T& b) const{
        R s = 0;
        for (typename T::const_iterator ai = a.begin(), bi = b.begin(); ai != a.end(); ++ai, ++bi){
            R d = static_cast<R>(*ai) - static_cast<R>(*bi);
            s += d*d;
        }
        return s;
    }
};

/// L1 norm
template <class T>
struct l1norm
{
	typedef typename T::value_type R;

	/// L1 distance between a and b.
	R operator() (const T& a, const T& b) const{
		R s = 0;
		for (typename T::const_iterator ai = a.begin(), bi = b.begin(); ai != a.end(); ++ai, ++bi){
			R d = static_cast<R>(*ai) - static_cast<R>(*bi);
			s += std::abs(d);
		}
		return s;
	}
};

static int lpnorm_p = 2;

// Run-time selective
template <class T>
struct lpnorm
{
	typedef typename T::value_type R;

	R operator() (const T& a, const T& b) const{
		R s = 0;
		switch (lpnorm_p){
			case 1:	s = l1norm<T>()(a,b);				break;
			case 2:	s = l2norm_squared<T>()(a,b);		break;
		}
		return s;
	}
};

} // namespace
