// [DynamicShadows] receiver, vertex side - INJECTED into the game's own object vertex shaders (built into the .so).
//   //@@decl   -> inserted before main()
//   //@@assign -> inserted right after the game computes "vec4 WorldPos = ObjMatrix * vec4(Position,1.0);"
//   //@@nl     -> inserted after "vec3 WorldNormal = ...;" (lit shaders only): w = N.L toward the sun, fades shadows on surfaces facing away
// PS_LightVP = the sun's view-projection matrix of the shadow map (row 2 = the light's forward axis).
//@@decl
varying highp vec4 Out_ShadowPos;
uniform highp mat4 PS_LightVP;
//@@assign

Out_ShadowPos = vec4((PS_LightVP * WorldPos).xyz, 1.0);
//@@nl

Out_ShadowPos.w = max(dot(-normalize(vec3(PS_LightVP[0][2], PS_LightVP[1][2], PS_LightVP[2][2])), normalize(WorldNormal)), 0.0);
//@@end
