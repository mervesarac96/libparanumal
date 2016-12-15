cl__1 = 1;
Point(1) = {-1, -1, 0, 1};
Point(2) = {1, -1, 0, 1};
Point(3) = {1, 1, 0, 1};
Point(4) = {-1, 1, 0, 1};
Point(5) = {0.25, 0, 0, 1};
Point(6) = {-0.25, 0.25, 0, 1};
Point(7) = {-0.25, -0.25, 0, 1};
Line(1) = {1, 2};
Line(2) = {2, 3};
Line(3) = {3, 4};
Line(4) = {4, 1};
Line(5) = {5, 6};
Line(6) = {6, 7};
Line(7) = {7, 5};
Line Loop(10) = {4, 1, 2, 3, -5, -7, -6};
Plane Surface(10) = {10};
Physical Line("Wall") = {1, 2, 3, 4};
Physical Line("Scatter") = {5, 6, 7};
Coherence;
Physical Surface("Domain", 1) += {10};
Physical Surface("Domain", 10) = {10};
