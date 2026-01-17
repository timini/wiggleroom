/*******************************************************************************
 * LINKAGE - Chaotic Percussion Generator
 *
 * Physical model of a chain of 3 masses connected by loose, non-linear springs.
 * Strike the top mass, listen to the bottom mass rattle.
 *
 * Parameters:
 *   - tension: Spring stiffness (10-200). High = metallic, Low = rattle
 *   - damping: Energy loss (0.001-0.5). High = dull thud, Low = long rattle
 *   - slack: Non-linearity threshold (0-0.5). Creates chaotic "clinking"
 *   - gate: Trigger to strike the chain
 ******************************************************************************/

import("stdfaust.lib");

// ==========================================================
// 1. CONTROLS
// ==========================================================

// TENSION (Spring Stiffness)
// High = Tight chain (Metallic tone)
// Low = Loose chain (Rattle/Shaker)
tension = hslider("tension", 50, 10, 200, 0.1) : si.smoo;

// DAMPING (Energy Loss)
// High = Dull thud, Low = Long rattle
damping = hslider("damping", 0.1, 0.001, 0.5, 0.001);

// SLACK (Non-Linearity)
// How much the chain can move before the spring engages.
// This creates the chaotic "clinking" sound.
slack = hslider("slack", 0.0, 0.0, 0.5, 0.01);

// EXCITATION
gate = button("gate");

// ==========================================================
// 2. PHYSICS ENGINE
// ==========================================================

// Force of a "Loose" Spring between two positions (p1, p2)
// If distance < slack, force is 0.
spring_force(p1, p2, k) = f
with {
    dist = p2 - p1;
    // Apply slack threshold - no force if within slack zone
    eff_dist = ba.if(abs(dist) < slack, 0, dist);
    f = eff_dist * k;
};

// The Chain Simulation Loop
// We track Velocity (v) and Position (p) for 3 masses.
// Returns: p1, v1, p2, v2, p3, v3
chain_sim(input_force) = loop ~ (_,_,_,_,_,_)
with {
    dt = 1.0 / ma.SR;
    mass = 1.0;

    loop(p1, v1, p2, v2, p3, v3) = (
        p1_new, v1_new,
        p2_new, v2_new,
        p3_new, v3_new
    )
    with {
        // --- FORCES ON MASS 1 (Top) ---
        // Connected to: Wall (0) and Mass 2
        f_wall = spring_force(0, p1, tension);
        f_m2   = spring_force(p2, p1, tension);
        f1_net = input_force - f_wall + f_m2 - (v1 * damping);

        v1_new = v1 + (f1_net / mass) * dt;
        p1_new = p1 + v1_new * dt;

        // --- FORCES ON MASS 2 (Middle) ---
        // Connected to: Mass 1 and Mass 3
        f_m1_down = spring_force(p1, p2, tension);
        f_m3_up   = spring_force(p3, p2, tension);
        f2_net    = -f_m1_down + f_m3_up - (v2 * damping);

        v2_new = v2 + (f2_net / mass) * dt;
        p2_new = p2 + v2_new * dt;

        // --- FORCES ON MASS 3 (Bottom) ---
        // Connected to: Mass 2
        f_m2_down = spring_force(p2, p3, tension);
        f3_net    = -f_m2_down - (v3 * damping);

        v3_new = v3 + (f3_net / mass) * dt;
        p3_new = p3 + v3_new * dt;
    };
};

// ==========================================================
// 3. OUTPUT PROCESSING
// ==========================================================

// DC blocker to remove any offset from the physics simulation
dc_block = fi.dcblocker;

// Lowpass filter to tame harsh high frequencies from the chain rattling
// This smooths out the "digital" quality without losing character
output_filter = fi.lowpass(2, 8000);

// Soft clipper
soft_clip(x) = ma.tanh(x);

// ==========================================================
// 4. MAIN PROCESS
// ==========================================================

// Generate Impulse -> Run Physics -> Output Bottom Mass Velocity
// Reduced impulse strength and added smoothing for cleaner attack
impulse = no.noise : fi.lowpass(1, 5000) : *(en.ar(0.002, 0.03, gate)) : *(50);

// Extract velocity of bottom mass (v3) from the 6-output chain
// Outputs are: p1, v1, p2, v2, p3, v3
// We want the 6th output (v3)
// Apply filtering and soft limiting for cleaner output
process = impulse : chain_sim : (!, !, !, !, !, _)
        : dc_block
        : output_filter
        : soft_clip
        : *(0.5);
