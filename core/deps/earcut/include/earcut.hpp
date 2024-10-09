#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

namespace mapbox {

namespace util {

template <std::size_t I, typename T> struct nth {
    inline static typename std::tuple_element<I, T>::type
    get(const T& t) { return std::get<I>(t); };
};

}

namespace detail {

template <typename N = uint32_t>
class Earcut {
public:
    std::vector<N> indices;
    N sumVertices = 0;

    template <typename Polygon>
    void operator()(const Polygon& points);

private:
    struct Node {

        Node(Node* prev_, N index, double x_, double y_)
            : x(x_), y(y_), i(index) {

            if (prev_) {
                next = prev_->next;
                prev = prev_;
                prev_->next->prev = this;
                prev_->next = this;
            } else {
                next = this;
                prev = this;
            }
        }

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        Node(Node&&) = delete;
        Node& operator=(Node&&) = delete;

        // previous and next vertice nodes in a polygon ring
        Node* next;
        Node* prev;

        const double x;
        const double y;

        // previous and next nodes in z-order
        Node* nextZ = nullptr;
        Node* prevZ = nullptr;

        // z-order curve value
        int32_t z = 0;

        // indicates whether this is a steiner point
        int8_t area = 0;
        bool steiner = false;

        const N i;
    };

    template <typename Ring> Node* linkedList(const Ring& points, const bool clockwise);
    Node* filterPoints(Node* start, Node* end = nullptr);
    void earcutLinked(Node* ear);
    void earcutLinkedRun(Node* ear);
    bool isEar(Node* ear);
    bool isEarHashed(Node* ear);
    Node* cureLocalIntersections(Node* start);
    void splitEarcut(Node* start);
    template <typename Polygon> Node* eliminateHoles(const Polygon& points, Node* outerNode);
    void eliminateHole(Node* hole, Node* outerNode);
    Node* findHoleBridge(Node* hole, Node* outerNode);
    void indexCurve(Node* start);
    Node* sortLinked(Node* list);
    int32_t zOrder(const double x_, const double y_);
    Node* getLeftmost(Node* start);
    bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) const;
    bool pointInTriangle(const Node& a, const Node& b, const Node& c, const Node& p) const;
    bool isValidDiagonal(const Node* a, const Node* b);
    double area(const Node* p, const Node* q, const Node* r) const;
    int8_t areaSign(const Node* q) const;
    void setAreaSign(Node* q);
    bool equals(const Node* p1, const Node* p2);
    bool intersects(const Node* p1, const Node* q1, const Node* p2, const Node* q2);
    bool intersectsPolygon(const Node* a, const Node* b);
    bool locallyInside(const Node* a, const Node* b);
    bool middleInside(const Node* a, const Node* b);
    Node* splitPolygon(Node* a, Node* b);
    template <typename Point> Node* insertNode(N i, const Point& p, Node* last);
    void removeNode(Node* p);

    bool hashing;
    double minX, maxX;
    double minY, maxY;
    double extents;
    double invExtents;

    template<typename T>
    inline double getX(T p) { return util::nth<0, T>::get(p); }

    template<typename T>
    inline double getY(T p) { return util::nth<1, T>::get(p); }

    template <typename T, typename Alloc = std::allocator<T>>
    class ObjectPool {
    public:
        ObjectPool() { }
        ObjectPool(std::size_t blockSize) {
            reset(blockSize);
        }
        ~ObjectPool() {
            for (auto allocation : allocations) {
                alloc.deallocate(allocation, blockSize);
            }
        }
        template <typename... Args>
        T* construct(Args&&... args) {
            if (currentIndex >= blockSize) {
                if (++blockIndex == allocations.size()){
                    //printf("alloc %d\n", blockIndex);
                    currentBlock = alloc.allocate(blockSize);
                    allocations.emplace_back(currentBlock);
                } else {
                    currentBlock = allocations[blockIndex];
                }
                currentIndex = 0;
            }
            T* object = &currentBlock[currentIndex++];
            alloc.construct(object, std::forward<Args>(args)...);
            return object;
        }
        void reset(std::size_t requestedSize) {
            if (allocations.empty()) {
                currentBlock = alloc.allocate(blockSize);
                allocations.emplace_back(currentBlock);
            } else {
                size_t nBlocks = requestedSize / blockSize + 1;
                if (nBlocks < allocations.size()) {
                    //printf("discard %d %d\n", allocations.size(),  nBlocks);
                    for (size_t i = nBlocks; i < allocations.size(); i++) {
                        alloc.deallocate(allocations[i], blockSize);
                    }
                    allocations.resize(nBlocks);
                }
            }
            currentIndex = 0;
            blockIndex = 0;
            currentBlock = allocations[blockIndex];
        }
        void clear() { reset(0); }
    private:
        T* currentBlock = nullptr;
        std::size_t currentIndex = 0;
        std::size_t blockIndex = 0;
        std::size_t blockSize = 1024;
        std::vector<T*> allocations;
        Alloc alloc;
    };
    ObjectPool<Node> nodes;

    std::vector<Node*> rings;
};

template <typename N> template <typename Polygon>
void Earcut<N>::operator()(const Polygon& points) {
    // reset
    indices.clear();
    sumVertices = 0;

    if (points.empty()) return;

    double x;
    double y;
    extents = 0;
    int threshold = 80;
    std::size_t sumPoints = 0;

    for (size_t i = 0; threshold >= 0 && i < points.size(); i++) {
        threshold -= points[i].size();
        sumPoints += points[i].size();
    }

    // estimate size of nodes and indices
    nodes.reset(sumPoints * 3 / 2);
    indices.reserve(sumPoints * 3);
    rings.clear();

    // before linkedList call, must be known in createNode
    hashing = threshold < 0;

    Node* outerNode = linkedList(points[0], true);
    if (!outerNode) return;

    // if the shape is not too simple, we'll use z-order curve hash later; calculate polygon bbox
    if (hashing) {
        Node* p = outerNode->next;
        minX = maxX = p->x;
        minY = maxY = p->y;
        do {
            x = p->x;
            y = p->y;
            minX = (std::min)(minX, x);
            minY = (std::min)(minY, y);
            maxX = (std::max)(maxX, x);
            maxY = (std::max)(maxY, y);
            p = p->next;
        } while (p != outerNode);

        // minX, minY and size are later used to transform coords into integers for z-order calculation
        extents = (std::max)(maxX - minX, maxY - minY);
        invExtents = 32767.0 / extents;
    }

    if (points.size() > 1) {
        outerNode = eliminateHoles(points, outerNode);
    }

    earcutLinked(outerNode);
}

// Create a circular doubly linked list from polygon points in the specified winding order.
// Clockwise means outer ring.
template <typename N> template <typename Ring>
typename Earcut<N>::Node*
Earcut<N>::linkedList(const Ring& points, const bool clockwise) {
    using Point = typename Ring::value_type;
    double sum = 0;
    const int len = points.size();

    Point p1 = points[0];
    Point p2 = points[len - 1];

    bool duplicate = getX(p1) == getX(p2) && getY(p1) == getY(p2);

    // calculate original winding order of a polygon ring
    for (int i = 0, j = len - 1; i < len; j = i++) {
        p1 = points[i];
        p2 = points[j];
        sum += (getX(p2) - getX(p1)) * (getY(p1) + getY(p2));
    }

    if (sum == 0) {
        sumVertices += len;
        return nullptr;
    }

    // link points into circular doubly-linked list in the specified winding order
    Node* last = nullptr;
    if (clockwise == (sum > 0)) {
        int end = duplicate ? len - 1 : len;

        for (int i = 0; i < end; i++) {
          last = insertNode(sumVertices + i, points[i], last);
        }
    } else {
        int start = len;
        int end = duplicate ? 1 : 0;

        for (int i = start - 1; i >= end; i--) {
            last = insertNode(sumVertices + i, points[i], last);
        }
    }

    sumVertices += len;

    if (!last) {
        sumVertices += len;
        return nullptr;
    }

    Node* node = last;
    do  {
        node = node->next;
        setAreaSign(node);
    } while (node != last);

    last = filterPoints(last);

    return last;
}

// eliminate colinear or duplicate points
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::filterPoints(Node* start, Node* end) {
    if (!end) end = start;

    Node* p = start;
    bool again;
    do {
        again = false;

        if ((equals(p, p->next) || areaSign(p) == 0) && !p->steiner) {
            removeNode(p);
            p = end = p->prev;

            if (p == p->next) return nullptr;
            again = true;

        } else {
            p = p->next;
        }
    } while (again || p != end);

    return end;
}

template <typename N>
void Earcut<N>::earcutLinked(Node* ear) {
    if (!ear) return;


    rings.push_back(ear);

    while (!rings.empty()) {
        Node* ring = rings.back();
        rings.pop_back();

        // interlink polygon nodes in z-order
        if (hashing) indexCurve(ring);

        earcutLinkedRun(ring);
    }
}

// main ear slicing loop which triangulates a polygon (given as a linked list)
template <typename N>
void Earcut<N>::earcutLinkedRun(Node* ear) {
    if (!ear) return;

    int pass = 0;

    Node* stop = ear;
    Node* prev;
    Node* next;

    // iterate through ears, slicing them one by one
    while (ear->prev != ear->next) {

        prev = ear->prev;
        next = ear->next;

        if (hashing ? isEarHashed(ear) : isEar(ear)) {
            // cut off the triangle
            indices.insert(indices.end(), {prev->i, ear->i, next->i});
            removeNode(ear);

            // skipping the next vertice leads to less sliver triangles
            ear = next->next;
            stop = next->next;

            continue;
        }

        ear = next;

        // if we looped through the whole remaining polygon and can't find any more ears
        if (ear == stop) {
            if (pass == 0) {
                // try filtering points and slicing again
                pass = 1;
                ear = stop = filterPoints(ear);
                if (!ear) break;

            } else if (pass == 1) {
                // if this didn't work, try curing all small self-intersections locally
                pass = 2;
                ear = stop = cureLocalIntersections(ear);
                if (!ear) break;

            } else if (pass == 2) {
                // as a last resort, try splitting the remaining polygon into two
                splitEarcut(ear);
                break;
            }
        }
    }
}

// check whether a polygon node forms a valid ear with adjacent nodes
template <typename N>
bool Earcut<N>::isEar(Node* ear) {
    const Node& a = *ear->prev;
    const Node& b = *ear;
    const Node& c = *ear->next;

    if (areaSign(ear) >= 0) return false; // reflex, can't be an ear

    const double minTX = (std::min)(a.x, (std::min)(b.x, c.x));
    const double minTY = (std::min)(a.y, (std::min)(b.y, c.y));
    const double maxTX = (std::max)(a.x, (std::max)(b.x, c.x));
    const double maxTY = (std::max)(a.y, (std::max)(b.y, c.y));

    // now make sure we don't have other points inside the potential ear
    Node* p = c.next;

    while (p != b.prev) {
        if (areaSign(p) >= 0 &&
            p->x >= minTX && p->x <= maxTX &&
            p->y >= minTY && p->y <= maxTY &&
            pointInTriangle(a, b, c, *p))
            return false;
        p = p->next;
    }

    return true;
}

template <typename N>
bool Earcut<N>::isEarHashed(Node* ear) {
    const Node& a = *ear->prev;
    const Node& b = *ear;
    const Node& c = *ear->next;

    if (areaSign(ear) >= 0) return false; // reflex, can't be an ear

    // triangle bbox; min & max are calculated like this for speed
    const double minTX = (std::min)(a.x, (std::min)(b.x, c.x));
    const double minTY = (std::min)(a.y, (std::min)(b.y, c.y));
    const double maxTX = (std::max)(a.x, (std::max)(b.x, c.x));
    const double maxTY = (std::max)(a.y, (std::max)(b.y, c.y));

    // z-order range for the current triangle bbox;
    const int32_t maxZ = zOrder(maxTX, maxTY);

    // first look for points inside the triangle in increasing z-order
    Node* p = ear->nextZ;

    while (p && p->z <= maxZ) {
        if (p != ear->prev && p != ear->next &&
            areaSign(p) >= 0 &&
            p->x >= minTX && p->x <= maxTX &&
            p->y >= minTY && p->y <= maxTY &&
            pointInTriangle(a, b, c, *p))
            return false;
        p = p->nextZ;
    }

    // then look for points in decreasing z-order
    const int32_t minZ = zOrder(minTX, minTY);

    p = ear->prevZ;

    while (p && p->z >= minZ) {
        if (p != ear->prev && p != ear->next &&
            areaSign(p) >= 0 &&
            p->x >= minTX && p->x <= maxTX &&
            p->y >= minTY && p->y <= maxTY &&
            pointInTriangle(a, b, c, *p))
            return false;
        p = p->prevZ;
    }

    return true;
}

// go through all polygon nodes and cure small local self-intersections
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::cureLocalIntersections(Node* start) {
    Node* p = start;
    do {
        Node* a = p->prev;
        Node* b = p->next->next;

        // a self-intersection where edge (v[i-1],v[i]) intersects (v[i+1],v[i+2])
        if (intersects(a, p, p->next, b) && locallyInside(a, b) && locallyInside(b, a)) {
            indices.insert(indices.end(), {a->i, p->i, b->i});

            // remove two nodes involved
            removeNode(p);
            removeNode(p->next);

            p = start = b;
        }
        p = p->next;
    } while (p != start);

    return p;
}

// try splitting polygon into two and triangulate them independently
template <typename N>
void Earcut<N>::splitEarcut(Node* start) {
    // look for a valid diagonal that divides the polygon into two
    Node* a = start;
    do {
        Node* b = a->next->next;
        while (b != a->prev) {
            if (a->i != b->i && isValidDiagonal(a, b)) {
                // split the polygon in two by the diagonal
                Node* c = splitPolygon(a, b);

                // filter colinear points around the cuts
                a = filterPoints(a, a->next);
                c = filterPoints(c, c->next);

                // run earcut on each half
                if (a) { rings.push_back(a); }
                if (c) { rings.push_back(c); }
                return;
            }
            b = b->next;
        }
        a = a->next;
    } while (a != start);
}

// link every hole into the outer loop, producing a single-ring polygon without holes
template <typename N> template <typename Polygon>
typename Earcut<N>::Node*
Earcut<N>::eliminateHoles(const Polygon& points, Node* outerNode) {
    const size_t len = points.size();

    std::vector<Node*> queue;
    for (size_t i = 1; i < len; i++) {
        Node* list = linkedList(points[i], false);
        if (list) {
            if (list == list->next) list->steiner = true;
            queue.push_back(getLeftmost(list));
        }
    }
    std::sort(queue.begin(), queue.end(), [this](const Node* a, const Node* b) {
        return a->x < b->x;
    });

    // process holes from left to right
    for (size_t i = 0; i < queue.size(); i++) {
        eliminateHole(queue[i], outerNode);
        outerNode = filterPoints(outerNode, outerNode->next);
    }

    return outerNode;
}

// find a bridge between vertices that connects hole with an outer ring and and link it
template <typename N>
void Earcut<N>::eliminateHole(Node* hole, Node* outerNode) {
    outerNode = findHoleBridge(hole, outerNode);
    if (outerNode) {
        Node* b = splitPolygon(outerNode, hole);
        filterPoints(b, b->next);
    }
}

// David Eberly's algorithm for finding a bridge between hole and outer polygon
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::findHoleBridge(Node* hole, Node* outerNode) {
    Node* p = outerNode;
    double hx = hole->x;
    double hy = hole->y;
    double qx = -std::numeric_limits<double>::infinity();
    Node* m = nullptr;

    // find a segment intersected by a ray from the hole's leftmost Vertex to the left;
    // segment's endpoint with lesser x will be potential connection Vertex
    do {
        if (hy <= p->y && hy >= p->next->y) {
          double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
          if (x <= hx && x > qx) {
            qx = x;
            m = p->x < p->next->x ? p : p->next;
          }
        }
        p = p->next;
    } while (p != outerNode);

    if (!m) { return nullptr; }

    if (hole->x == m->x) { return m->prev; }

    // look for points inside the triangle of hole Vertex, segment intersection and endpoint;
    // if there are no points found, we have a valid connection;
    // otherwise choose the Vertex of the minimum angle with the ray as connection Vertex

    const Node* stop = m;
    double tanMin = std::numeric_limits<double>::infinity();
    double tanCur = 0;

    p = m->next;
    double mx = m->x;
    double my = m->y;

    Node* inside = nullptr;
    double nearestX = -std::numeric_limits<double>::infinity();

    while (p != stop) {
        if (hx >= p->x && p->x >= mx &&
            pointInTriangle(hy < my ? hx : qx, hy,
                            mx, my,
                            hy < my ? qx : hx, hy,
                            p->x, p->y)) {


            tanCur = std::abs(hy - p->y) / (hx - p->x); // tangential

            // 6. If any reflex point lies within the triangle, take the
            //    point with the least angle between the horizontal ray and the segment
            //    <h,p> (<M,R>). If the angle is equal take the one that is nearer to M

            // Must find a point inside triangle
            // m = nullptr;

            if ((tanCur < tanMin || (tanCur == tanMin && p->x > nearestX))
                && areaSign(p) != 0
                && locallyInside(p, hole)) {

                nearestX = p->x;
                inside = p;
                tanMin = tanCur;
            }
        }
        p = p->next;
    }

    if (inside) { return inside; }

    return m;
}

// interlink polygon nodes in z-order
template <typename N>
void Earcut<N>::indexCurve(Node* start) {
    assert(start);
    Node* p = start;

    do {
        p->z = p->z ? p->z : zOrder(p->x, p->y);
        p->prevZ = p->prev;
        p->nextZ = p->next;
        p = p->next;
    } while (p != start);

    p->prevZ->nextZ = nullptr;
    p->prevZ = nullptr;

    sortLinked(p);
}

// Simon Tatham's linked list merge sort algorithm
// http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::sortLinked(Node* list) {
    assert(list);
    int inSize = 1;

    while (true) {
        Node* p = list;
        list = nullptr;

        Node *tail = nullptr;
        int numMerges = 0;

        while (p) {
            numMerges++;
            Node* q = p;
            int pSize = 0;
            for (int i = 0; i < inSize; i++) {
                pSize++;
                q = q->nextZ;
                if (!q) break;
            }

            int qSize = inSize;

            while (pSize > 0 || (qSize > 0 && q)) {
                Node *e;

                if (pSize == 0) {
                    e = q;
                    q = q->nextZ;
                    qSize--;
                } else if (qSize == 0 || !q) {
                    e = p;
                    p = p->nextZ;
                    pSize--;
                } else if (p->z <= q->z) {
                    e = p;
                    p = p->nextZ;
                    pSize--;
                } else {
                    e = q;
                    q = q->nextZ;
                    qSize--;
                }

                if (tail) tail->nextZ = e;
                else list = e;

                e->prevZ = tail;
                tail = e;
            }

            p = q;
        }

        tail->nextZ = nullptr;

        if (numMerges <= 1) return list;

        inSize *= 2;
    }
}

// z-order of a Vertex given coords and size of the data bounding box
template <typename N>
int32_t Earcut<N>::zOrder(const double x_, const double y_) {
    // coords are transformed into non-negative 15-bit integer range
    int32_t x = static_cast<int32_t>((x_ - minX) * invExtents);
    int32_t y = static_cast<int32_t>((y_ - minY) * invExtents);

    x = (x | (x << 8)) & 0x00FF00FF;
    x = (x | (x << 4)) & 0x0F0F0F0F;
    x = (x | (x << 2)) & 0x33333333;
    x = (x | (x << 1)) & 0x55555555;

    y = (y | (y << 8)) & 0x00FF00FF;
    y = (y | (y << 4)) & 0x0F0F0F0F;
    y = (y | (y << 2)) & 0x33333333;
    y = (y | (y << 1)) & 0x55555555;

    return x | (y << 1);
}

// find the leftmost node of a polygon ring
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::getLeftmost(Node* start) {
    Node* p = start;
    Node* leftmost = start;
    do {
        if (p->x < leftmost->x) leftmost = p;
        p = p->next;
    } while (p != start);

    return leftmost;
}

// check if a point lies within a convex triangle
template <typename N>
bool Earcut<N>::pointInTriangle(double ax, double ay, double bx, double by,
                                double cx, double cy, double px, double py) const {
    return (cx - px) * (ay - py) - (ax - px) * (cy - py) >= 0 &&
           (ax - px) * (by - py) - (bx - px) * (ay - py) >= 0 &&
           (bx - px) * (cy - py) - (cx - px) * (by - py) >= 0;
}

template <typename N>
bool Earcut<N>::pointInTriangle(const Node& a, const Node& b,
                                const Node& c, const Node& p) const {
    return (c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y) >= 0 &&
           (a.x - p.x) * (b.y - p.y) - (b.x - p.x) * (a.y - p.y) >= 0 &&
           (b.x - p.x) * (c.y - p.y) - (c.x - p.x) * (b.y - p.y) >= 0;
}

// check if a diagonal between two polygon nodes is valid (lies in polygon interior)
template <typename N>
bool Earcut<N>::isValidDiagonal(const Node* a, const Node* b) {
    return equals(a, b) || (a->next->i != b->i && a->prev->i != b->i && !intersectsPolygon(a, b) &&
           locallyInside(a, b) && locallyInside(b, a) && middleInside(a, b));
}

// signed area of a triangle
template <typename N>
double Earcut<N>::area(const Node* p, const Node* q, const Node* r) const {
    return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
}

template <typename N>
int8_t Earcut<N>::areaSign(const Node* q) const {
    return q->area;
}

template <typename N>
void Earcut<N>::setAreaSign(Node* q) {
    const Node* p = q->prev;
    const Node* r = q->next;
    double a = (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
    q->area = a > 0.0 ? 1 : a < 0.0 ? -1 : 0;
}

// check if two points are equal
template <typename N>
bool Earcut<N>::equals(const Node* p1, const Node* p2) {
    return p1->x == p2->x && p1->y == p2->y;
}

// check if two segments intersect
template <typename N>
bool Earcut<N>::intersects(const Node* p1, const Node* q1, const Node* p2, const Node* q2) {
    return ((area(p1, q1, p2) > 0) != (area(p1, q1, q2) > 0) &&
            (area(p2, q2, p1) > 0) != (area(p2, q2, q1) > 0));
}

// check if a polygon diagonal intersects any polygon segments
template <typename N>
bool Earcut<N>::intersectsPolygon(const Node* a, const Node* b) {
    const Node* p = a;
    do {
        if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i &&
                intersects(p, p->next, a, b)) return true;
        p = p->next;
    } while (p != a);

    return false;
}

// check if a polygon diagonal is locally inside the polygon
template <typename N>
bool Earcut<N>::locallyInside(const Node* a, const Node* b) {
    return areaSign(a) < 0 ?
        area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0 :
        area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
}

// check if the middle Vertex of a polygon diagonal is inside the polygon
template <typename N>
bool Earcut<N>::middleInside(const Node* a, const Node* b) {
    const Node* p = a;
    bool inside = false;
    double px = (a->x + b->x) / 2;
    double py = (a->y + b->y) / 2;
    do {
        if (((p->y > py) != (p->next->y > py)) &&
            (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
            inside = !inside;
        p = p->next;
    } while (p != a);

    return inside;
}

// link two polygon vertices with a bridge; if the vertices belong to the same ring, it splits
// polygon into two; if one belongs to the outer ring and another to a hole, it merges it into a
// single ring
template <typename N>
typename Earcut<N>::Node*
Earcut<N>::splitPolygon(Node* a, Node* b) {
    Node* a2 = nodes.construct(nullptr, a->i, a->x, a->y);
    Node* b2 = nodes.construct(nullptr, b->i, b->x, b->y);

    Node* an = a->next;
    Node* bp = b->prev;

    a->next = b;
    b->prev = a;

    a2->next = an;
    an->prev = a2;

    b2->next = a2;
    a2->prev = b2;

    bp->next = b2;
    b2->prev = bp;

    setAreaSign(a);
    setAreaSign(a2);
    setAreaSign(an);

    setAreaSign(b);
    setAreaSign(b2);
    setAreaSign(bp);

    return b2;
}

// create a node and util::optionally link it with previous one (in a circular doubly linked list)
template <typename N> template <typename Point>
typename Earcut<N>::Node*
Earcut<N>::insertNode(N i, const Point& pt, Node* last) {
    return nodes.construct(last, i, getX(pt), getY(pt));
}

template <typename N>
void Earcut<N>::removeNode(Node* p) {
    p->next->prev = p->prev;
    p->prev->next = p->next;

    if (hashing) {
        if (p->prevZ) p->prevZ->nextZ = p->nextZ;
        if (p->nextZ) p->nextZ->prevZ = p->prevZ;
    }

    setAreaSign(p->next);
    setAreaSign(p->prev);
}
}

template <typename N = uint32_t, typename Polygon>
std::vector<N> earcut(const Polygon& poly) {
    mapbox::detail::Earcut<N> earcut;
    earcut(poly);
    return earcut.indices;
}
}
