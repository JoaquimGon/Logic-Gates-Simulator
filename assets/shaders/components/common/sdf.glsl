// Native distances stay in sync with Components/Definitions/PresentationGeometry.cpp.

float sdBox(vec2 p, vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdRightTriangle(vec2 p, float halfW, float halfH)
{
    float backEdge = -p.x - halfW;
    float slopedEdges = abs(p.y) - halfH * (halfW - p.x) / (2.0 * halfW);
    return max(backEdge, slopedEdges);
}

float sdAndGate(vec2 p, vec2 halfSize)
{
    p.y = abs(p.y);

    // Split point where the flat top/bottom ends and the curved cap begins.
    // The curve radius matches the half-height (halfSize.y) so it meets the walls seamlessly.
    float splitX = halfSize.x - halfSize.y;

    // Union the cap with the straight body: their internal join is not an edge.
    float boxHalfW = (splitX + halfSize.x) * 0.5;
    float boxCenterX = -halfSize.x + boxHalfW;
    vec2 q = abs(p - vec2(boxCenterX, 0.0)) - vec2(boxHalfW, halfSize.y);
    float straight = min(max(q.x, q.y), 0.0) + length(max(q, 0.0));
    float cap = length(p - vec2(splitX, 0.0)) - halfSize.y;
    return min(straight, cap);
}

float sdCircle(vec2 p, vec2 center, float r)
{
    return length(p - center) - r;
}

float sdOrGate(vec2 p)
{
    // Lens body: circles intersect at tip
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334;

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.4, 0.0);
    float backR = 0.98;
    float backCut = sdCircle(p, backCenter, backR);

    // Carve back cavity out of the lens body
    return max(lens, -backCut);
}

float sdXorGate(vec2 p)
{
    // Lens body: circles intersect at tip (+0.49, 0.0)
    float Rc = 1.05;
    float cy = 0.65;
    float cx = -0.334;

    float topCircle = sdCircle(p, vec2(cx,  cy), Rc);
    float botCircle = sdCircle(p, vec2(cx, -cy), Rc);
    float lens = max(topCircle, botCircle);

    // Concave back cut
    vec2 backCenter = vec2(-1.27, 0.0);
    float backR = 0.98; // Apex sits at -1.27 + 0.98 = -0.29
    float backCut = sdCircle(p, backCenter, backR);
    float body = max(lens, -backCut);

    return body;
}
