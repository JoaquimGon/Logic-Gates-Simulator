#version 330 core
out vec4 FragColor;

in vec2 localPos; // Received from vertex shader (-0.5 to 0.5)

// Signed distance for a box
float sdBox( in vec2 p, in vec2 b )
{
    vec2 d = abs(p) - b;
    return length(max(d,0.0)) + min(max(d.x,d.y),0.0);
}

// Signed distance for an infinite vertical strip/semi-circle combination
float sdAndGate(in vec2 p)
{
    // 1. The left rectangular body of the AND gate
    // p.x is shifted so the box sits on the left side
    vec2 boxPos = p - vec2(-0.1, 0.0);
    float box = sdBox(boxPos, vec2(0.2, 0.3));

    // 2. The rounded front cap on the right side (using a vertical range or circle)
    // We can create a pill/capsule shape or circle arc for the right side.
    // Let's use a circle centered on the right edge:
    vec2 circleCenter = vec2(0.1, 0.0);
    float circle = length(p - circleCenter) - 0.3; // radius 0.3

    // To cut the circle so it only forms the right half-dome:
    // We union the box and restrict the circle using max()
    // Or simpler: combine a box and a proper half-circle shape.
    
    // Let's use a clean combination: Box on the left, and a smooth union with a right-side rounded cap
    // Actually, a classic procedural AND gate uses a box for the back and a half-circle for the front:
    
    // Let's define the box part (from x = -0.3 to 0.1)
    float mainBody = sdBox(p - vec2(-0.1, 0.0), vec2(0.2, 0.3));
    
    // The rounded right end (a semi-circle anchored at x = 0.1)
    // Distance to a vertical line at x = 0.1, capped at top/bottom y = 0.3
    vec2 q = p;
    q.x = q.x - 0.1; // Shift center to the right flat edge
    
    // If x > 0.0, it's the rounded front dome part
    float frontDome = (q.x > 0.0) ? (length(q) - 0.3) : (abs(q.y) - 0.3);

    // Combine them smoothly (min combines shapes together)
    // For an exact geometric AND gate, a rounded box with specific dimensions actually gets very close, 
    // but splitting it into a box + right cap gives a pristine D-shape:
    
    // Let's use a cleaner union of a left box and a right semi-circle:
    vec2 leftBoxP = p;
    leftBoxP.x = max(leftBoxP.x, 0.0); // restrict box to left side
    float leftBox = sdBox(p - vec2(-0.1, 0.0), vec2(0.2, 0.3));
    
    // Better yet, let's use the exact Inigo Quilez formula for a stadium/capsule or pure D-shape:
    // D-shape: Box combined with a right-side circular arc
    float boxPart = sdBox(p - vec2(-0.1, 0.0), vec2(0.2, 0.3));
    
    // Let's use a precise D-shape math approach:
    // Width = 0.4, Height = 0.6
    // We can clamp the x coordinate for the round front:
    vec2 dCoord = p;
    dCoord.x = dCoord.x - 0.1; // Center of the round cap
    
    // Standard D-Shape SDF:
    float k = 0.3; // radius of the round end
    float dx = abs(dCoord.x) - 0.1;
    float dy = abs(dCoord.y) - k;
    
    // Combining box and round front smoothly:
    // If x is on the left, it's a flat edge. If x is on the right, it curves like a circle.
    float dBox = sdBox(p - vec2(-0.15, 0.0), vec2(0.15, 0.3));
    float dSemiCircle = length(p - vec2(0.0, 0.0)) - 0.3; // approximate
    
    // Let's use the cleanest exact D-shape math:
    vec2 pos = p;
    pos.x -= 0.05; // Shift slightly
    float sx = pos.x;
    float sy = abs(pos.y);
    
    // Distance field for a D-shape (flat left, curved right)
    float b_width = 0.591;
    float b_height = 0.591;
    
    // Inside/outside calculation for D-shape:
    vec2 boxP = pos - vec2(-b_width*0.5, 0.0);
    float b = sdBox(boxP, vec2(b_width*0.5, b_height));
    
    // Right cap arc
    vec2 capP = pos - vec2(0.0, 0.0);
    float cap = (pos.x > 0.0) ? (length(pos) - b_height) : (abs(pos.y) - b_height);

    return min(b, cap);
}

void main()
{
    vec2 p = localPos * 1.3; // Scale to give room

    float d = sdAndGate(p);

    // Smooth edge fill
    float fillFactor = 1.0 - smoothstep(0.0, 0.005, d);

    vec3 gateColor = vec3(0.2, 0.5, 0.9);
    vec3 bgColor   = vec3(0.1, 0.1, 0.1); 

    vec3 finalColor = mix(bgColor, gateColor, fillFactor);

    FragColor = vec4(finalColor, 1.0);
}