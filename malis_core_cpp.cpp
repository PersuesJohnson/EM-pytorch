#include <iostream>
#include <cstdlib>
#include <cmath>
#include <boost/pending/disjoint_sets.hpp>
#include <vector>
#include <queue>
#include <map>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>

using namespace std;

template <class T>
class AffinityGraphCompare{
    private:
        const T * mEdgeWeightArray;
    public:
        AffinityGraphCompare(const T * EdgeWeightArray){
            mEdgeWeightArray = EdgeWeightArray;
        }
        bool operator() (const int& ind1, const int& ind2) const {
            return (mEdgeWeightArray[ind1] > mEdgeWeightArray[ind2]);
        }
};

/*
 * Compute the MALIS loss function and its derivative wrt the affinity graph
 * MAXIMUM spanning tree
 * Author: Srini Turaga (sturaga@mit.edu)
 * All rights reserved
 */
void preCompute(const uint64_t* conn_dims, const int32_t* nhood_data, const uint64_t* nhood_dims,
        uint64_t* pre_ve, uint64_t* pre_prodDims, int32_t* pre_nHood){

  uint64_t conn_num_dims = nhood_dims[1]+1; //4
  // nVert stores (x * y * z)
  uint64_t nVert = 1;
  for (int64_t i = 1; i < conn_num_dims; ++i) {
      nVert *= conn_dims[i];
  }
  pre_ve[0] = nVert;
  /* get number of edges */
  uint64_t nEdge = 0;
  // Loop over #edges
  for (int d = 0, i = 0; d < conn_dims[0]; ++d) {
    // Loop over Z
    for (int z = 0; z < conn_dims[1]; ++z) {
      // Loop over Y
      for (int y = 0; y < conn_dims[2]; ++y) {
        // Loop over X
        for (int x = 0; x < conn_dims[3]; ++x, ++i) {
          // Out-of-bounds check:
          if (!((z + nhood_data[d * nhood_dims[1] + 0] < 0)
              ||(z + nhood_data[d * nhood_dims[1] + 0] >= conn_dims[1])
              ||(y + nhood_data[d * nhood_dims[1] + 1] < 0)
              ||(y + nhood_data[d * nhood_dims[1] + 1] >= conn_dims[2])
              ||(x + nhood_data[d * nhood_dims[1] + 2] < 0)
              ||(x + nhood_data[d * nhood_dims[1] + 2] >= conn_dims[3]))) {
              ++nEdge;
          }
        }
      }
    }
  }
  pre_ve[1] = nEdge;

  // prodDims stores x, x*y, x*y*z offsets
  pre_prodDims[conn_num_dims - 2] = 1;
  for (int64_t i = 1; i < conn_num_dims - 1; ++i) {
      pre_prodDims[conn_num_dims - 2 - i] = pre_prodDims[conn_num_dims - 1 - i]
                                      * conn_dims[conn_num_dims - i];
  }

  /* convert n-d offset vectors into linear array offset scalars */
  // nHood is a vector of size #edges
    for (int64_t i = 0; i < nhood_dims[0]; ++i) {
        pre_nHood[i] = 0;
        for (int64_t j = 0; j < nhood_dims[1]; ++j) {
            pre_nHood[i] += nhood_data[j + i * nhood_dims[1]] * pre_prodDims[j];
        }
    }
}

const std::vector<int64_t> & initPqueue(const int64_t nEdge,
               const uint64_t* conn_dims, const int32_t* nhood_data, const uint64_t* nhood_dims ){
    static std::vector<int64_t> pqueue;
    if(pqueue.empty()){
        // initialization
        pqueue.resize(nEdge);
        int j = 0;
          // Loop over #edges
          for (int d = 0, i = 0; d < conn_dims[0]; ++d) {
            // Loop over Z
            for (int z = 0; z < conn_dims[1]; ++z) {
              // Loop over Y
              for (int y = 0; y < conn_dims[2]; ++y) {
                // Loop over X
                for (int x = 0; x < conn_dims[3]; ++x, ++i) {
                  // Out-of-bounds check:
                  if (!((z + nhood_data[d * nhood_dims[1] + 0] < 0)
                      ||(z + nhood_data[d * nhood_dims[1] + 0] >= conn_dims[1])
                      ||(y + nhood_data[d * nhood_dims[1] + 1] < 0)
                      ||(y + nhood_data[d * nhood_dims[1] + 1] >= conn_dims[2])
                      ||(x + nhood_data[d * nhood_dims[1] + 2] < 0)
                      ||(x + nhood_data[d * nhood_dims[1] + 2] >= conn_dims[3]))) {
                        pqueue[j++] = i;
                  }
                }
              }
            }
          }
        }
    return pqueue;
}

// for a given segmentation map (if not all 0), there are two cases:
// 1. only positive edges: single segmentation label
// 2. both positive and negative edges
void malis_loss_weights_cpp_pos(const uint64_t* seg,
               const uint64_t* conn_dims, const int32_t* nhood_data, const uint64_t* nhood_dims,
               const float* edgeWeight,
               float* nPairPerEdge,
               const uint64_t* pre_ve, const uint64_t* pre_prodDims, const int32_t* pre_nHood, const int pos){

    /* Disjoint sets and sparse overlap vectors */
    const int64_t nVert = pre_ve[0];
    const int64_t nEdge = pre_ve[1];
    uint64_t conn_num_dims = nhood_dims[1]+1;
    
    vector<map<uint64_t,uint64_t> > overlap(nVert);
    vector<uint64_t> rank(nVert);
    vector<uint64_t> parent(nVert);
    boost::disjoint_sets<uint64_t*, uint64_t*> dsets(&rank[0],&parent[0]);
    std::map<int64_t, int64_t> segSizes;
    int nLabeledVert = 0;
    int nPairPos = 0;
    for (int i=0; i<nVert; ++i){
        dsets.make_set(i);
        if (0!=seg[i]) {
            overlap[i].insert(pair<uint64_t,uint64_t>(seg[i],1));
            ++nLabeledVert;
            ++segSizes[seg[i]];
            nPairPos += (segSizes[seg[i]] - 1); //add pair with previous seg label
        }
    }

    float nPairPosNormInv;
    if (pos==1){
        nPairPosNormInv = 1.0/(float)nPairPos;
    }else{
        nPairPosNormInv = 1.0/(nLabeledVert*(nLabeledVert-1)*0.5-nPairPos);
    }

    /* 2. Sort all the edges in increasing order of weight */
    const std::vector<int64_t> Pqueue1 = initPqueue(nEdge, conn_dims, nhood_data, nhood_dims);
    static std::vector<int64_t> Pqueue2(nEdge);
    std::copy(Pqueue1.begin(), Pqueue1.end(), Pqueue2.begin());
   
    sort( Pqueue2.begin(), Pqueue2.end(), AffinityGraphCompare<float>( edgeWeight ) );

    /* Start MST */
    int minEdge, e, v1, v2;
    uint64_t set1, set2;
    float nPair = 0;
    map<uint64_t,uint64_t>::iterator it1, it2;

    // std::ofstream db;
    // db.open("/n/coxfs01/donglai/malis_trans/db/mst-pt2.txt");
    /* Start Kruskal's */
    for (unsigned int i = 0; i < Pqueue2.size(); ++i ) {
        minEdge = Pqueue2[i];
        // std::cout <<"do:"<<i<<","<<minEdge<< "\n";
        e =  minEdge / nVert;
        // v1: node at edge beginning
        v1 = minEdge % nVert;
        // v2: neighborhood node at edge e
        v2 = v1 + pre_nHood[e];
        set1 = dsets.find_set(v1);
        set2 = dsets.find_set(v2);
        if (set1!=set2){
            dsets.link(set1, set2);
            /* compute the number of pairs merged by this MST edge */
            for (it1 = overlap[set1].begin();
                    it1 != overlap[set1].end(); ++it1) {
                for (it2 = overlap[set2].begin();
                        it2 != overlap[set2].end(); ++it2) {
                    nPair = (float)(it1->second * it2->second);
                    if ((pos==1 && it1->first == it2->first) || (pos==0 && it1->first != it2->first)) { //pos
                        nPairPerEdge[minEdge] += nPairPosNormInv * nPair;
                    }
                }
            }
            /* move the pixel bags of the non-representative to the representative */
            if (dsets.find_set(set1) == set2) // make set1 the rep to keep and set2 the rep to empty
                swap(set1,set2);
            it2 = overlap[set2].begin();
            while (it2 != overlap[set2].end()) {
                it1 = overlap[set1].find(it2->first);
                if (it1 == overlap[set1].end()) {
                    overlap[set1].insert(pair<uint64_t,uint64_t>(it2->first,it2->second));
                } else {
                    it1->second += it2->second;
                }
                overlap[set2].erase(it2++);
            }
        } // end link
    } // end while
    //db.close();
}
// doesn't work: can't sort edgeWeight
void malis_loss_weights_cpp_pair(const uint64_t* seg,
               const uint64_t* conn_dims, const int32_t* nhood_data, const uint64_t* nhood_dims,
               const float* edgeWeight,
               float* nPairPerEdge,
               const uint64_t* pre_ve, const uint64_t* pre_prodDims, const int32_t* pre_nHood){

    /* Disjoint sets and sparse overlap vectors */
    const uint64_t nVert = pre_ve[0];
    const uint64_t nEdge = pre_ve[1];
    uint64_t conn_num_dims = nhood_dims[1]+1;
    
    vector<map<uint64_t,uint64_t> > overlap(nVert);
    vector<uint64_t> rank(nVert);
    vector<uint64_t> parent(nVert);
    boost::disjoint_sets<uint64_t*, uint64_t*> dsets(&rank[0],&parent[0]);
    std::map<int64_t, int64_t> segSizes;
    int nLabeledVert = 0;
    int nPairPos = 0;
    for (int i=0; i<nVert; ++i){
        dsets.make_set(i);
        if (0!=seg[i]) {
            overlap[i].insert(pair<uint64_t,uint64_t>(seg[i],1));
            ++nLabeledVert;
            ++segSizes[seg[i]];
            nPairPos += (segSizes[seg[i]] - 1); //add pair with previous seg label
        }
    }

    int nPairTot = (nLabeledVert * (nLabeledVert - 1)) / 2;
    int nPairNeg = nPairTot - nPairPos;
    float nPairPosNormInv = 1.0/(float)nPairPos;
    float nPairNegNormInv = 1.0/(float)nPairNeg;
 

    /* 2. Sort all the edges in increasing order of weight */
    const std::vector<int64_t> Pqueue1 = initPqueue(nEdge, conn_dims, nhood_data, nhood_dims);
    static std::vector<int64_t> Pqueue2(nEdge);
    std::copy(Pqueue1.begin(), Pqueue1.end(), Pqueue2.begin());
   
    sort( Pqueue2.begin(), Pqueue2.end(), AffinityGraphCompare<float>( edgeWeight ) );

    /* Start MST */
    int minEdge, e, v1, v2;
    uint64_t set1, set2;
    float nPair = 0;
    map<uint64_t,uint64_t>::iterator it1, it2;
    /* Start Kruskal's */

    for (unsigned int i = 0; i < Pqueue2.size(); ++i ) {
        minEdge = Pqueue2[i];
        e =  minEdge / nVert;
        // v1: node at edge beginning
        v1 = minEdge % nVert;
        // v2: neighborhood node at edge e
        v2 = v1 + pre_nHood[e];
        set1 = dsets.find_set(v1);
        set2 = dsets.find_set(v2);
        if (set1!=set2){
            dsets.link(set1, set2);
            /* compute the number of pairs merged by this MST edge */
            for (it1 = overlap[set1].begin();
                    it1 != overlap[set1].end(); ++it1) {
                for (it2 = overlap[set2].begin();
                        it2 != overlap[set2].end(); ++it2) {
                    nPair = (float)(it1->second * it2->second);
                    if (it1->first == it2->first) { // pos: same seg
                        nPairPerEdge[minEdge] += nPairPosNormInv * nPair;
                    } else{
                        nPairPerEdge[minEdge] -= nPairNegNormInv * nPair;
                    }
                }
            }
            /* move the pixel bags of the non-representative to the representative */
            if (dsets.find_set(set1) == set2) // make set1 the rep to keep and set2 the rep to empty
                swap(set1,set2);
            it2 = overlap[set2].begin();
            while (it2 != overlap[set2].end()) {
                it1 = overlap[set1].find(it2->first);
                if (it1 == overlap[set1].end()) {
                    overlap[set1].insert(pair<uint64_t,uint64_t>(it2->first,it2->second));
                } else {
                    it1->second += it2->second;
                }
                overlap[set2].erase(it2++);
            }
        } // end link
    } // end while
}


void malis_loss_weights_cpp(const uint64_t* seg,
               const uint64_t* conn_dims, const int32_t* nhood_data, const uint64_t* nhood_dims,
               const float* edgeWeight,
               const int pos,
               float* nPairPerEdge){

  uint64_t conn_num_dims = nhood_dims[1]+1;
  /* Cache for speed to access neighbors */
  // nVert stores (x * y * z)
  int64_t nVert = 1;
  for (int64_t i = 1; i < conn_num_dims; ++i) {
      nVert *= conn_dims[i];
  }

  // prodDims stores x, x*y, x*y*z offsets
  std::vector<int64_t> prodDims(conn_num_dims - 1);
  prodDims[conn_num_dims - 2] = 1;
  for (int64_t i = 1; i < conn_num_dims - 1; ++i) {
      prodDims[conn_num_dims - 2 - i] = prodDims[conn_num_dims - 1 - i]
                                      * conn_dims[conn_num_dims - i];
  }

  /* convert n-d offset vectors into linear array offset scalars */
  // nHood is a vector of size #edges
    std::vector<int32_t> nHood(nhood_dims[0]);
    for (int64_t i = 0; i < nhood_dims[0]; ++i) {
        nHood[i] = 0;
        for (int64_t j = 0; j < nhood_dims[1]; ++j) {
            nHood[i] += (int32_t) nhood_data[j + i * nhood_dims[1]] * prodDims[j];
        }
    }

    /* Disjoint sets and sparse overlap vectors */
    vector<map<uint64_t,uint64_t> > overlap(nVert);
    vector<uint64_t> rank(nVert);
    vector<uint64_t> parent(nVert);
    boost::disjoint_sets<uint64_t*, uint64_t*> dsets(&rank[0],&parent[0]);
    std::map<int64_t, int64_t> segSizes;
    int nLabeledVert = 0;
    int nPairPos = 0;
    for (int i=0; i<nVert; ++i){
        dsets.make_set(i);
        if (0!=seg[i]) {
            overlap[i].insert(pair<uint64_t,uint64_t>(seg[i],1));
            ++nLabeledVert;
            ++segSizes[seg[i]];
            nPairPos += (segSizes[seg[i]] - 1); //add pair with previous seg label
        }
    }

    int nPairTot = (nLabeledVert * (nLabeledVert - 1)) / 2;
    int nPairNeg = nPairTot - nPairPos;
    float nPairNormR;
    if(pos==1){
        nPairNormR = (float)nPairPos;
    }else{
        nPairNormR = (float)nPairNeg;
    }

    if (nPairNormR ==0){
        return;
    }else{
        nPairNormR = 1.0/nPairNormR;
    }
    /* get number of edges */
  int nEdge = 0;
  // Loop over #edges
  for (int d = 0, i = 0; d < conn_dims[0]; ++d) {
    // Loop over Z
    for (int z = 0; z < conn_dims[1]; ++z) {
      // Loop over Y
      for (int y = 0; y < conn_dims[2]; ++y) {
        // Loop over X
        for (int x = 0; x < conn_dims[3]; ++x, ++i) {
          // Out-of-bounds check:
          if (!((z + nhood_data[d * nhood_dims[1] + 0] < 0)
              ||(z + nhood_data[d * nhood_dims[1] + 0] >= conn_dims[1])
              ||(y + nhood_data[d * nhood_dims[1] + 1] < 0)
              ||(y + nhood_data[d * nhood_dims[1] + 1] >= conn_dims[2])
              ||(x + nhood_data[d * nhood_dims[1] + 2] < 0)
              ||(x + nhood_data[d * nhood_dims[1] + 2] >= conn_dims[3]))) {
              ++nEdge;
          }
        }
      }
    }
  }
    /* Sort all the edges in increasing order of weight */
    std::vector< int > pqueue( nEdge );
    int j = 0;
      // Loop over #edges
      for (int d = 0, i = 0; d < conn_dims[0]; ++d) {
        // Loop over Z
        for (int z = 0; z < conn_dims[1]; ++z) {
          // Loop over Y
          for (int y = 0; y < conn_dims[2]; ++y) {
            // Loop over X
            for (int x = 0; x < conn_dims[3]; ++x, ++i) {
              // Out-of-bounds check:
              if (!((z + nhood_data[d * nhood_dims[1] + 0] < 0)
                  ||(z + nhood_data[d * nhood_dims[1] + 0] >= conn_dims[1])
                  ||(y + nhood_data[d * nhood_dims[1] + 1] < 0)
                  ||(y + nhood_data[d * nhood_dims[1] + 1] >= conn_dims[2])
                  ||(x + nhood_data[d * nhood_dims[1] + 2] < 0)
                  ||(x + nhood_data[d * nhood_dims[1] + 2] >= conn_dims[3]))) {
                    pqueue[j++] = i;
              }
            }
          }
        }
      }
    pqueue.resize(j);
    sort( pqueue.begin(), pqueue.end(), AffinityGraphCompare<float>( edgeWeight ) );

    /* Start MST */
    int minEdge, e, v1, v2;
    uint64_t set1, set2;
    float nPair = 0;
    map<uint64_t,uint64_t>::iterator it1, it2;

    /* Start Kruskal's */
    // std::ofstream db;
    // db.open("/n/coxfs01/donglai/malis_trans/db/mst-pt.txt");
    
    //std::cout<<"hi:"<<std::endl;for (unsigned int i = 0; i < pqueue.size(); ++i ) {std::cout<<i<<",";}
    for (unsigned int i = 0; i < pqueue.size(); ++i ) {
        minEdge = pqueue[i];
        e =  minEdge / nVert;
        // v1: node at edge beginning
        v1 = minEdge % nVert;
        // v2: neighborhood node at edge e
        v2 = v1 + nHood[e];

        set1 = dsets.find_set(v1);
        set2 = dsets.find_set(v2);

        if (set1!=set2){
            dsets.link(set1, set2);

            /* compute the number of pairs merged by this MST edge */
            for (it1 = overlap[set1].begin();
                    it1 != overlap[set1].end(); ++it1) {
                for (it2 = overlap[set2].begin();
                        it2 != overlap[set2].end(); ++it2) {

                    nPair = (float)(it1->second * it2->second);

                    if (pos>0 && (it1->first == it2->first)) {
                        nPairPerEdge[minEdge] += nPairNormR * nPair;
                        // db <<minEdge<<","<<edgeWeight[minEdge]<<","<<v1<<","<<v2<<","<<nPair << "\n";
                    } else if ((pos==0) && (it1->first != it2->first)) {
                        nPairPerEdge[minEdge] += nPairNormR * nPair;
                    }
                }
            }

            /* move the pixel bags of the non-representative to the representative */
            if (dsets.find_set(set1) == set2) // make set1 the rep to keep and set2 the rep to empty
                swap(set1,set2);

            it2 = overlap[set2].begin();
            while (it2 != overlap[set2].end()) {
                it1 = overlap[set1].find(it2->first);
                if (it1 == overlap[set1].end()) {
                    overlap[set1].insert(pair<uint64_t,uint64_t>(it2->first,it2->second));
                } else {
                    it1->second += it2->second;
                }
                overlap[set2].erase(it2++);
            }
        } // end link

    } // end while
    //db.close();
}

void connected_components_cpp(const int nVert,
               const int nEdge, const uint64_t* node1, const uint64_t* node2, const int* edgeWeight,
               uint64_t* seg){

    /* Make disjoint sets */
    vector<uint64_t> rank(nVert);
    vector<uint64_t> parent(nVert);
    boost::disjoint_sets<uint64_t*, uint64_t*> dsets(&rank[0],&parent[0]);
    for (int i=0; i<nVert; ++i)
        dsets.make_set(i);

    /* union */
    for (int i = 0; i < nEdge; ++i )
         // check bounds to make sure the nodes are valid
        if ((edgeWeight[i]!=0) && (node1[i]>=0) && (node1[i]<nVert) && (node2[i]>=0) && (node2[i]<nVert))
            dsets.union_set(node1[i],node2[i]);

    /* find */
    for (int i = 0; i < nVert; ++i)
        seg[i] = dsets.find_set(i);
}


void marker_watershed_cpp(const int nVert, const uint64_t* marker,
               const int nEdge, const uint64_t* node1, const uint64_t* node2, const float* edgeWeight,
               uint64_t* seg){

    /* Make disjoint sets */
    vector<uint64_t> rank(nVert);
    vector<uint64_t> parent(nVert);
    boost::disjoint_sets<uint64_t*, uint64_t*> dsets(&rank[0],&parent[0]);
    for (uint64_t i=0; i<nVert; ++i)
        dsets.make_set(i);

    /* initialize output array and find representatives of each class */
    std::map<uint64_t,uint64_t> components;
    for (uint64_t i=0; i<nVert; ++i){
        seg[i] = marker[i];
        if (seg[i] > 0)
            components[seg[i]] = i;
    }

    // merge vertices labeled with the same marker
    for (uint64_t i=0; i<nVert; ++i)
        if (seg[i] > 0)
            dsets.union_set(components[seg[i]],i);

    /* Sort all the edges in decreasing order of weight */
    std::vector<int> pqueue( nEdge );
    int j = 0;
    for (int i = 0; i < nEdge; ++i)
        if ((edgeWeight[i]!=0) &&
            (node1[i]>=0) && (node1[i]<nVert) &&
            (node2[i]>=0) && (node2[i]<nVert) &&
            (marker[node1[i]]>=0) && (marker[node2[i]]>=0))
                pqueue[ j++ ] = i;
    unsigned long nValidEdge = j;
    pqueue.resize(nValidEdge);
    sort( pqueue.begin(), pqueue.end(), AffinityGraphCompare<float>( edgeWeight ) );

    /* Start MST */
	int e;
    int set1, set2, label_of_set1, label_of_set2;
    for (unsigned int i = 0; i < pqueue.size(); ++i ) {
		e = pqueue[i];
        set1=dsets.find_set(node1[e]);
        set2=dsets.find_set(node2[e]);
        label_of_set1 = seg[set1];
        label_of_set2 = seg[set2];

        if ((set1!=set2) &&
            ( ((label_of_set1==0) && (marker[set1]==0)) ||
             ((label_of_set2==0) && (marker[set1]==0))) ){

            dsets.link(set1, set2);
            // either label_of_set1 is 0 or label_of_set2 is 0.
            seg[dsets.find_set(set1)] = std::max(label_of_set1,label_of_set2);
            
        }

    }

    // write out the final coloring
    for (int i=0; i<nVert; i++)
        seg[i] = seg[dsets.find_set(i)];

}
