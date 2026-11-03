import * as THREE from "three";

import {
  OrbitControls
} from "three/examples/jsm/controls/OrbitControls.js";

import {
  CSS2DObject,
  CSS2DRenderer
} from "three/examples/jsm/renderers/CSS2DRenderer.js";

import type {
  Face,
  ParsedSmd,
  Vec3
} from "../smd/types";

import {
  triangulateFace
} from "./triangulation";


export interface ViewerHandle {

  load(
    parsed: ParsedSmd
  ): void;

  clear(): void;

  fit(): void;

  setVerticesVisible(
    visible: boolean
  ): void;

  setHoveredVertex(
    index: number | null
  ): void;

  setSelectedVertex(
    index: number | null
  ): void;

  setVertexHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  setVertexSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  setFaceSelectMode(
    enabled: boolean
  ): void;

  setFaceColorsEnabled(
  enabled: boolean
): void;

  setHoveredFace(
    index: number | null
  ): void;

  setSelectedFace(
    index: number | null
  ): void;

  setFaceHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  setFaceSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  dispose(): void;
}


const DEFAULT_VERTEX_COLOR =
  0x2563eb;

const HOVER_VERTEX_COLOR =
  0xf59e0b;

const FACE_VERTEX_COLOR =
  0xdc2626;

const SELECTED_VERTEX_COLOR =
  0x7c3aed;

const MUTED_VERTEX_COLOR =
  0x94a3b8;

const DEFAULT_FACE_COLOR =
  0xc9cdd2;

const HOVER_FACE_COLOR =
  0xfbbf24;

const SELECTED_FACE_COLOR =
  0x60a5fa;

const FACE_COLOR_PALETTE = [

  0x90caf9,

  0xa5d6a7,

  0xffcc80,

  0xce93d8,

  0x80cbc4,

  0xef9a9a,

  0xfff59d,

  0xb0bec5,

  0x9fa8da,

  0xbcaaa4,

  0x80deea,

  0xc5e1a5

];

type VertexNode =
  THREE.Mesh<
    THREE.SphereGeometry,
    THREE.MeshBasicMaterial
  >;


type FaceNode =
  THREE.Mesh<
    THREE.BufferGeometry,
    THREE.MeshStandardMaterial
  >;


function formatCoordinate(
  value: number
): string {

  const normalized =
    Math.abs(value) < 1e-9
      ? 0
      : value;

  const rounded =
    Math.round(normalized);

  if (
    Math.abs(
      normalized - rounded
    ) < 1e-6
  ) {
    return String(
      rounded
    );
  }

  return normalized.toFixed(3);
}


function vertexLabelText(
  index: number,
  vertex: Vec3
): string {

  return (
    `V${index} (` +
    `${formatCoordinate(vertex.x)}, ` +
    `${formatCoordinate(vertex.y)}, ` +
    `${formatCoordinate(vertex.z)})`
  );
}


function buildCombinedGeometry(
  parsed: ParsedSmd
): THREE.BufferGeometry {

  const vertices =
    parsed.geometry.brep.verticesWorld;

  const positions: number[] =
    [];

  for (
    const face of
    parsed.geometry.brep.faces
  ) {

    const triangles =
      triangulateFace(
        vertices,
        face
      );

    for (
      const triangle of triangles
    ) {

      for (
        const vertexIndex of triangle
      ) {

        const vertex =
          vertices[vertexIndex];

        positions.push(
          vertex.x,
          vertex.y,
          vertex.z
        );
      }
    }
  }

  const geometry =
    new THREE.BufferGeometry();

  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      positions,
      3
    )
  );

  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  return geometry;
}


function buildFaceGeometry(
  vertices: Vec3[],
  face: Face
): THREE.BufferGeometry {

  const positions: number[] =
    [];

  const triangles =
    triangulateFace(
      vertices,
      face
    );

  for (
    const triangle of triangles
  ) {

    for (
      const vertexIndex of triangle
    ) {

      const vertex =
        vertices[vertexIndex];

      positions.push(
        vertex.x,
        vertex.y,
        vertex.z
      );
    }
  }

  const geometry =
    new THREE.BufferGeometry();

  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      positions,
      3
    )
  );

  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  return geometry;
}


function makeGrid(
  size: number
): THREE.GridHelper {

  const safeSize =
    Math.max(
      size,
      1000
    );

  const grid =
    new THREE.GridHelper(
      safeSize,
      20
    );

  grid.rotation.x =
    Math.PI / 2;

  return grid;
}


function getModelSize(
  vertices: Vec3[]
): number {

  if (
    vertices.length === 0
  ) {
    return 1000;
  }

  let minX = Infinity;
  let minY = Infinity;
  let minZ = Infinity;

  let maxX = -Infinity;
  let maxY = -Infinity;
  let maxZ = -Infinity;

  for (
    const vertex of vertices
  ) {

    minX =
      Math.min(
        minX,
        vertex.x
      );

    minY =
      Math.min(
        minY,
        vertex.y
      );

    minZ =
      Math.min(
        minZ,
        vertex.z
      );

    maxX =
      Math.max(
        maxX,
        vertex.x
      );

    maxY =
      Math.max(
        maxY,
        vertex.y
      );

    maxZ =
      Math.max(
        maxZ,
        vertex.z
      );
  }

  return Math.max(
    maxX - minX,
    maxY - minY,
    maxZ - minZ,
    1
  );
}


export function createViewer(
  container: HTMLElement
): ViewerHandle {

  THREE.Object3D.DEFAULT_UP.set(
    0,
    0,
    1
  );

  const scene =
    new THREE.Scene();

  scene.background =
    new THREE.Color(
      0xf4f6f8
    );


  const camera =
    new THREE.PerspectiveCamera(
      45,
      1,
      1,
      1_000_000
    );

  camera.up.set(
    0,
    0,
    1
  );


  const renderer =
    new THREE.WebGLRenderer({
      antialias: true
    });

  renderer.setPixelRatio(
    Math.min(
      window.devicePixelRatio,
      2
    )
  );

  container.appendChild(
    renderer.domElement
  );


  const labelRenderer =
    new CSS2DRenderer();

  labelRenderer.domElement.className =
    "vertex-label-layer";

  labelRenderer.domElement.style.position =
    "absolute";

  labelRenderer.domElement.style.left =
    "0";

  labelRenderer.domElement.style.top =
    "0";

  labelRenderer.domElement.style.pointerEvents =
    "none";

  container.appendChild(
    labelRenderer.domElement
  );


  const controls =
    new OrbitControls(
      camera,
      renderer.domElement
    );

  controls.enableDamping =
    true;

  controls.dampingFactor =
    0.08;


  const ambient =
    new THREE.AmbientLight(
      0xffffff,
      1.8
    );

  scene.add(
    ambient
  );


  const keyLight =
    new THREE.DirectionalLight(
      0xffffff,
      2.3
    );

  keyLight.position.set(
    1,
    -2,
    3
  );

  scene.add(
    keyLight
  );


  const fillLight =
    new THREE.DirectionalLight(
      0xffffff,
      1.2
    );

  fillLight.position.set(
    -2,
    1,
    1
  );

  scene.add(
    fillLight
  );


  const axes =
    new THREE.AxesHelper(
      1000
    );

  scene.add(
    axes
  );


  const raycaster =
    new THREE.Raycaster();

  const pointer =
    new THREE.Vector2();


  let modelRoot:
    THREE.Group | undefined;

  let vertexRoot:
    THREE.Group | undefined;

  let grid:
    THREE.GridHelper | undefined;


  let verticesVisible =
    false;

  let faceSelectMode =
    false;

  let faceColorsEnabled =
  false;


  let hoveredVertexIndex:
    number | null =
    null;
  
  let selectedVertexIndex:
  number | null =
  null;

  let hoveredFaceIndex:
    number | null =
    null;

  let selectedFaceIndex:
    number | null =
    null;


  let lastViewerHoverIndex:
    number | null =
    null;

  let lastViewerFaceHoverIndex:
    number | null =
    null;


  let vertexHoverHandler:
    (
      index: number | null
    ) => void =
    () => undefined;
  
  let vertexSelectionHandler:

    (
      index: number | null
    ) => void =

    () => undefined;


  let faceHoverHandler:
    (
      index: number | null
    ) => void =
    () => undefined;


  let faceSelectionHandler:
    (
      index: number | null
    ) => void =
    () => undefined;


  const vertexNodes:
    VertexNode[] =
    [];

  const vertexLabels:
    HTMLElement[] =
    [];

  const vertexLabelObjects:
    CSS2DObject[] =
    [];

  const faceMeshes:
    FaceNode[] =
    [];

  const faceVertexIndices:
    number[][] =
    [];


  function activeFaceVertexSet():
    Set<number> {

    const activeFace =
      hoveredFaceIndex !== null
        ? hoveredFaceIndex
        : selectedFaceIndex;

    if (
      activeFace === null
      ||
      activeFace < 0
      ||
      activeFace >=
        faceVertexIndices.length
    ) {
      return new Set<number>();
    }

    return new Set<number>(
      faceVertexIndices[
        activeFace
      ]
    );
  }


  function applyVertexAppearance(): void {

    const faceVertices =
      activeFaceVertexSet();


    /*
    * Persistent selection only has
    * the "mute others" effect while
    * Show Vertices is ON.
    */
    const selectionActive =

      verticesVisible

      &&

      selectedVertexIndex !==
        null;


    vertexNodes.forEach(
      (
        node,
        index
      ) => {

        const isHovered =
          index ===
          hoveredVertexIndex;


        const isSelected =
          index ===
          selectedVertexIndex;


        const isFaceVertex =
          faceVertices.has(
            index
          );


        /*
        * V1.0.2 Day 2:
        *
        * A vertex can be visible because:
        *
        * 1. Show Vertices is ON
        * 2. table hover is temporarily
        *    inspecting it
        * 3. it is persistently selected
        * 4. it contributes to an active face
        */
        node.visible =

          verticesVisible

          ||

          isHovered

          ||

          isSelected

          ||

          isFaceVertex;


        /*
        * Appearance priority:
        *
        * hover
        *   >
        * selected vertex
        *   >
        * face-related vertex
        *   >
        * muted vertex
        *   >
        * normal vertex
        */

        if (
          isHovered
        ) {

          node.material.color.setHex(
            HOVER_VERTEX_COLOR
          );


          node.scale.setScalar(
            2.5
          );


        } else if (
          isSelected
        ) {

          node.material.color.setHex(
            SELECTED_VERTEX_COLOR
          );


          node.scale.setScalar(
            2.2
          );


        } else if (
          isFaceVertex
        ) {

          node.material.color.setHex(
            FACE_VERTEX_COLOR
          );


          node.scale.setScalar(
            1.8
          );


        } else if (
          selectionActive
        ) {

          node.material.color.setHex(
            MUTED_VERTEX_COLOR
          );


          node.scale.setScalar(
            0.85
          );


        } else {

          node.material.color.setHex(
            DEFAULT_VERTEX_COLOR
          );


          node.scale.setScalar(
            1
          );
        }
      }
    );


    vertexLabels.forEach(
      (
        label,
        index
      ) => {

        const isHovered =
          index ===
          hoveredVertexIndex;


        const isSelected =
          index ===
          selectedVertexIndex;


        const isFaceVertex =
          faceVertices.has(
            index
          );


        const isMuted =

          selectionActive

          &&

          !isSelected

          &&

          !isHovered

          &&

          !isFaceVertex;


        label.classList.toggle(
          "is-hovered",
          isHovered
        );


        label.classList.toggle(
          "is-selected",
          isSelected
        );


        label.classList.toggle(
          "is-face-related",
          isFaceVertex
        );


        label.classList.toggle(
          "is-muted",
          isMuted
        );
      }
    );


    vertexLabelObjects.forEach(
      (
        labelObject,
        index
      ) => {

        labelObject.visible =

          verticesVisible

          ||

          index ===
            hoveredVertexIndex

          ||

          index ===
            selectedVertexIndex

          ||

          faceVertices.has(
            index
          );
      }
    );
  }

  function baseFaceColor(
  faceIndex: number
  ): number {

    if (
      !faceColorsEnabled
    ) {

      return DEFAULT_FACE_COLOR;
    }


    return FACE_COLOR_PALETTE[
      faceIndex
      %
      FACE_COLOR_PALETTE.length
    ];
  }

  function applyFaceAppearance(): void {

      faceMeshes.forEach(
        (
          mesh,
          index
        ) => {

          /*
          * Priority:
          *
          * hover
          *   >
          * selected
          *   >
          * multicolor palette
          *   >
          * default gray
          */

          if (
            index ===
            hoveredFaceIndex
          ) {

            mesh.material.color.setHex(
              HOVER_FACE_COLOR
            );

            return;
          }


          if (
            index ===
            selectedFaceIndex
          ) {

            mesh.material.color.setHex(
              SELECTED_FACE_COLOR
            );

            return;
          }


          mesh.material.color.setHex(
            baseFaceColor(
              index
            )
          );
        }
      );
  }


  function setHoveredVertex(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        vertexNodes.length
        ? index
        : null;

    if (
      hoveredVertexIndex ===
      normalizedIndex
    ) {
      return;
    }

    hoveredVertexIndex =
      normalizedIndex;

    applyVertexAppearance();
  }

  function setSelectedVertex(
  index: number | null
  ): void {

    const normalizedIndex =

      index !== null

      &&

      index >= 0

      &&

      index <
        vertexNodes.length

        ? index

        : null;


    if (
      selectedVertexIndex ===
      normalizedIndex
    ) {

      return;
    }


    selectedVertexIndex =
      normalizedIndex;


    applyVertexAppearance();
  }


  function emitVertexHover(
    index: number | null
  ): void {

    setHoveredVertex(
      index
    );

    if (
      lastViewerHoverIndex ===
      index
    ) {
      return;
    }

    lastViewerHoverIndex =
      index;

    vertexHoverHandler(
      index
    );
  }

  function emitVertexSelection(
    index: number | null
  ): void {

    setSelectedVertex(
      index
    );


    /*
    * Inform main.ts so the
    * matching table row gets
    * persistent selection styling.
    */
    vertexSelectionHandler(
      index
    );
  }


  function setVertexHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    vertexHoverHandler =
      handler;
  }

  function setVertexSelectionHandler(

    handler:
      (
        index: number | null
      ) => void

  ): void {

    vertexSelectionHandler =
      handler;
  }


  function setHoveredFace(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        faceMeshes.length
        ? index
        : null;

    if (
      hoveredFaceIndex ===
      normalizedIndex
    ) {
      return;
    }

    hoveredFaceIndex =
      normalizedIndex;

    applyFaceAppearance();

    applyVertexAppearance();
  }


  function emitFaceHover(
    index: number | null
  ): void {

    setHoveredFace(
      index
    );

    if (
      lastViewerFaceHoverIndex ===
      index
    ) {
      return;
    }

    lastViewerFaceHoverIndex =
      index;

    faceHoverHandler(
      index
    );
  }


  function setSelectedFace(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        faceMeshes.length
        ? index
        : null;

    if (
      selectedFaceIndex ===
      normalizedIndex
    ) {
      return;
    }

    selectedFaceIndex =
      normalizedIndex;

    applyFaceAppearance();

    applyVertexAppearance();
  }


  function emitFaceSelection(
    index: number | null
  ): void {

    setSelectedFace(
      index
    );

    faceSelectionHandler(
      index
    );
  }


  function setFaceHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    faceHoverHandler =
      handler;
  }


  function setFaceSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    faceSelectionHandler =
      handler;
  }


function setFaceSelectMode(
  enabled: boolean
  ): void {

      faceSelectMode =
        enabled;


      if (
        enabled
      ) {

        /*
        * Face mode has priority
        * over vertex picking.
        */
        emitVertexHover(
          null
        );

      } else {

        /*
        * V1.0.2:
        *
        * Leaving Face Select mode
        * returns the face inspector
        * to a completely neutral
        * state.
        */

        emitFaceHover(
          null
        );


        emitFaceSelection(
          null
        );
      }


      renderer.domElement.style.cursor =
        "";
  }

  function setFaceColorsEnabled(
  enabled: boolean
  ): void {

    faceColorsEnabled =
      enabled;


    applyFaceAppearance();
  }


  function clearVertexRoot(): void {

    emitVertexHover(
      null
    );

    emitVertexSelection(
      null
    );

    vertexNodes.length =
      0;

    vertexLabels.length =
      0;

    vertexLabelObjects.length =
      0;

    if (
      !vertexRoot
    ) {
      return;
    }

    scene.remove(
      vertexRoot
    );

    vertexRoot.traverse(
      (
        object:
        THREE.Object3D
      ) => {

        if (
          object instanceof
          THREE.Mesh
        ) {

          object.geometry.dispose();

          if (
            Array.isArray(
              object.material
            )
          ) {

            object.material.forEach(
              (
                material:
                THREE.Material
              ) => {
                material.dispose();
              }
            );

          } else {

            object.material.dispose();
          }
        }


        if (
          object instanceof
          CSS2DObject
        ) {
          object.element.remove();
        }
      }
    );

    vertexRoot =
      undefined;
  }


  function clear(): void {

    emitVertexHover(
      null
    );

    emitFaceHover(
      null
    );

    selectedFaceIndex =
      null;

    faceMeshes.length =
      0;

    faceVertexIndices.length =
      0;


    if (
      modelRoot
    ) {

      scene.remove(
        modelRoot
      );

      modelRoot.traverse(
        (
          object:
          THREE.Object3D
        ) => {

          if (
            object instanceof
            THREE.Mesh
            ||
            object instanceof
            THREE.LineSegments
          ) {

            object.geometry.dispose();

            if (
              Array.isArray(
                object.material
              )
            ) {

              object.material.forEach(
                (
                  material:
                  THREE.Material
                ) => {
                  material.dispose();
                }
              );

            } else {

              object.material.dispose();
            }
          }
        }
      );

      modelRoot =
        undefined;
    }


    clearVertexRoot();


    if (
      grid
    ) {

      scene.remove(
        grid
      );

      grid.geometry.dispose();

      if (
        Array.isArray(
          grid.material
        )
      ) {

        grid.material.forEach(
          (
            material:
            THREE.Material
          ) => {
            material.dispose();
          }
        );

      } else {

        grid.material.dispose();
      }

      grid =
        undefined;
    }
  }


  function fit(): void {

    if (
      !modelRoot
    ) {
      return;
    }

    const box =
      new THREE.Box3()
        .setFromObject(
          modelRoot
        );

    if (
      box.isEmpty()
    ) {
      return;
    }

    const size =
      new THREE.Vector3();

    const center =
      new THREE.Vector3();

    box.getSize(
      size
    );

    box.getCenter(
      center
    );

    const maxDimension =
      Math.max(
        size.x,
        size.y,
        size.z,
        1
      );

    const fovRadians =
      THREE.MathUtils.degToRad(
        camera.fov
      );

    const distance =
      (
        maxDimension
        /
        (
          2
          *
          Math.tan(
            fovRadians / 2
          )
        )
      )
      *
      1.8;

    camera.position.set(
      center.x
        +
        distance * 0.8,

      center.y
        -
        distance * 0.9,

      center.z
        +
        distance * 0.65
    );

    camera.near =
      Math.max(
        distance / 10000,
        0.1
      );

    camera.far =
      distance * 100;

    camera.updateProjectionMatrix();

    controls.target.copy(
      center
    );

    controls.update();
  }


  function buildVertexNodes(
    parsed: ParsedSmd
  ): void {

    clearVertexRoot();

    const vertices =
      parsed.geometry
        .brep
        .verticesWorld;

    const root =
      new THREE.Group();

    root.name =
      "vertex-debug";

    /*
     * Root stays enabled.
     *
     * Individual nodes/labels
     * control their own visibility.
     */
    root.visible =
      true;

    const modelSize =
      getModelSize(
        vertices
      );

    const radius =
      Math.max(
        modelSize * 0.006,
        8
      );


    for (
      let index = 0;
      index < vertices.length;
      index += 1
    ) {

      const vertex =
        vertices[index];


      const nodeGeometry =
        new THREE.SphereGeometry(
          radius,
          12,
          8
        );

      const nodeMaterial =
        new THREE.MeshBasicMaterial({
          color:
            DEFAULT_VERTEX_COLOR
        });

      const node:
        VertexNode =
        new THREE.Mesh(
          nodeGeometry,
          nodeMaterial
        );

      node.position.set(
        vertex.x,
        vertex.y,
        vertex.z
      );

      node.name =
        `V${index}`;

      node.userData.vertexIndex =
        index;

      root.add(
        node
      );

      vertexNodes.push(
        node
      );


      const labelElement =
        document.createElement(
          "div"
        );

      labelElement.className =
        "vertex-label";

      labelElement.textContent =
        vertexLabelText(
          index,
          vertex
        );

      labelElement.dataset.vertexIndex =
        String(index);

      vertexLabels.push(
        labelElement
      );


      const labelObject =
        new CSS2DObject(
          labelElement
        );

      labelObject.position.set(
        vertex.x,
        vertex.y,
        vertex.z
      );

      labelObject.name =
        `V${index}-label`;

      labelObject.userData.vertexIndex =
        index;

      root.add(
        labelObject
      );

      vertexLabelObjects.push(
        labelObject
      );
    }


    vertexRoot =
      root;

    scene.add(
      root
    );

    applyVertexAppearance();
  }


  function buildFaceMeshes(
    parsed: ParsedSmd,
    root: THREE.Group
  ): void {

    const vertices =
      parsed.geometry
        .brep
        .verticesWorld;

    const faces =
      parsed.geometry
        .brep
        .faces;


    faces.forEach(
      (
        face,
        index
      ) => {

        const geometry =
          buildFaceGeometry(
            vertices,
            face
          );

        const material =
          new THREE.MeshStandardMaterial({
            color:
              DEFAULT_FACE_COLOR,

            roughness:
              0.82,

            metalness:
              0,

            side:
              THREE.DoubleSide
          });

        const mesh:
          FaceNode =
          new THREE.Mesh(
            geometry,
            material
          );

        mesh.name =
          `F${index}`;

        mesh.userData.faceIndex =
          index;

        root.add(
          mesh
        );

        faceMeshes.push(
          mesh
        );

        faceVertexIndices.push(
          [...face.indices]
        );
      }
    );
  }


  function load(
    parsed: ParsedSmd
  ): void {

    clear();

    const root =
      new THREE.Group();


    /*
     * Day 4:
     * each STRAKON face becomes
     * one independent Mesh.
     */
    buildFaceMeshes(
      parsed,
      root
    );


    /*
     * We still build one combined
     * geometry only for the black
     * edge overlay.
     */
    const combinedGeometry =
      buildCombinedGeometry(
        parsed
      );

    const edgeGeometry =
      new THREE.EdgesGeometry(
        combinedGeometry,
        1
      );

    combinedGeometry.dispose();

    const edgeMaterial =
      new THREE.LineBasicMaterial({
        color:
          0x222222
      });

    const edges =
      new THREE.LineSegments(
        edgeGeometry,
        edgeMaterial
      );

    root.add(
      edges
    );


    modelRoot =
      root;

    scene.add(
      root
    );


    buildVertexNodes(
      parsed
    );


    const box =
      new THREE.Box3()
        .setFromObject(
          root
        );

    const size =
      new THREE.Vector3();

    box.getSize(
      size
    );

    grid =
      makeGrid(
        Math.max(
          size.x,
          size.y,
          size.z
        )
        *
        2.5
      );

    scene.add(
      grid
    );

    fit();
  }


  function setVerticesVisible(
    visible: boolean
  ): void {

    verticesVisible =
      visible;


    /*
    * Turning Show Vertices OFF means:
    *
    * - no persistent vertex selection
    * - no current vertex hover
    *
    * Temporary table hover can create
    * a new hover again afterwards.
    */
    if (
      !visible
    ) {

      emitVertexHover(
        null
      );


      emitVertexSelection(
        null
      );


      renderer
        .domElement
        .style
        .cursor =
        "";
    }


    applyVertexAppearance();
  }


  function updatePointer(
    event: PointerEvent
  ): void {

    const rect =
      renderer.domElement
        .getBoundingClientRect();

    pointer.x =
      (
        (
          event.clientX
          -
          rect.left
        )
        /
        rect.width
      )
      *
      2
      -
      1;

    pointer.y =
      -
      (
        (
          event.clientY
          -
          rect.top
        )
        /
        rect.height
      )
      *
      2
      +
      1;
  }

  function raycastVertex(
    event: PointerEvent
  ): number | null {

    updatePointer(
      event
    );


    raycaster.setFromCamera(
      pointer,
      camera
    );


    /*
    * Only currently visible vertex
    * nodes should be pickable.
    */
    const visibleNodes =
      vertexNodes.filter(
        (node) =>
          node.visible
      );


    const intersections =
      raycaster.intersectObjects(
        visibleNodes,
        false
      );


    const first =
      intersections[0];


    if (
      !first
    ) {

      return null;
    }


    const index =
      Number(
        first
          .object
          .userData
          .vertexIndex
      );


    return Number.isInteger(
      index
    )
      ? index
      : null;
  }

  


  function raycastFace(
    event: PointerEvent
  ): number | null {

    updatePointer(
      event
    );

    raycaster.setFromCamera(
      pointer,
      camera
    );

    const intersections =
      raycaster.intersectObjects(
        faceMeshes,
        false
      );

    const first =
      intersections[0];

    if (
      !first
    ) {
      return null;
    }

    const index =
      Number(
        first.object
          .userData
          .faceIndex
      );

    return Number.isInteger(
      index
    )
      ? index
      : null;
  }


function handlePointerMove(
  event: PointerEvent
): void {

  /*
   * ---------------------------------------------
   * FACE MODE
   * ---------------------------------------------
   *
   * Face Select mode has priority.
   *
   * While Face Select is ON:
   *
   * - vertex hover is cleared
   * - mouse movement raycasts faces
   * - hovered face is reported to main.ts
   */
  if (
    faceSelectMode
  ) {

    emitVertexHover(
      null
    );


    const faceIndex =
      raycastFace(
        event
      );


    emitFaceHover(
      faceIndex
    );


    renderer
      .domElement
      .style
      .cursor =

      faceIndex !== null

        ? "pointer"

        : "";


    return;
  }


  /*
   * ---------------------------------------------
   * FACE MODE OFF
   * ---------------------------------------------
   *
   * Clear any temporary 3D face hover.
   *
   * Persistent face selection should already
   * have been cleared when Face Select mode
   * was turned OFF by setFaceSelectMode().
   */
  emitFaceHover(
    null
  );


  /*
   * ---------------------------------------------
   * VERTEX MODE
   * ---------------------------------------------
   *
   * Direct 3D vertex hovering is available
   * only while Show Vertices is ON.
   *
   * When Show Vertices is OFF, temporary
   * vertex inspection is initiated from the
   * vertex TABLE instead.
   */
  if (
    !verticesVisible

    ||

    vertexNodes.length === 0
  ) {

    emitVertexHover(
      null
    );


    renderer
      .domElement
      .style
      .cursor =
      "";


    return;
  }


  /*
   * Find the visible vertex currently
   * underneath the pointer.
   */
  const vertexIndex =
    raycastVertex(
      event
    );


  /*
   * This updates:
   *
   * - 3D dot hover appearance
   * - coordinate-label hover appearance
   * - main.ts callback
   * - matching vertex-table row
   */
  emitVertexHover(
    vertexIndex
  );


  /*
   * Indicate that the visible vertex
   * can be clicked for persistent
   * selection.
   */
  renderer
    .domElement
    .style
    .cursor =

    vertexIndex !== null

      ? "pointer"

      : "";
}


  function handlePointerLeave(): void {

    emitVertexHover(
      null
    );

    emitFaceHover(
      null
    );

    renderer.domElement.style.cursor =
      "";
  }


  function handlePointerClick(
    event: PointerEvent
  ): void {

    /*
    * ---------------------------------------------
    * FACE MODE
    * ---------------------------------------------
    *
    * Face Select mode has priority over
    * vertex selection.
    */
    if (
      faceSelectMode
    ) {

      const faceIndex =
        raycastFace(
          event
        );


      if (
        faceIndex === null
      ) {

        emitFaceSelection(
          null
        );


        return;
      }


      const nextFaceSelection =

        selectedFaceIndex ===
        faceIndex

          ? null

          : faceIndex;


      emitFaceSelection(
        nextFaceSelection
      );


      return;
    }


    /*
    * ---------------------------------------------
    * VERTEX MODE
    * ---------------------------------------------
    *
    * Persistent vertex selection is allowed
    * only when Show Vertices is ON.
    */
    if (
      !verticesVisible
    ) {

      return;
    }


    const vertexIndex =
      raycastVertex(
        event
      );


    /*
    * Clicking anywhere in the viewer that
    * is NOT a vertex clears the current
    * persistent vertex selection.
    *
    * This is the requested V1.0.2 behavior.
    */
    if (
      vertexIndex === null
    ) {

      emitVertexSelection(
        null
      );


      return;
    }


    /*
    * Clicking the currently selected
    * vertex again also deselects it.
    */
    const nextVertexSelection =

      selectedVertexIndex ===
      vertexIndex

        ? null

        : vertexIndex;


    emitVertexSelection(
      nextVertexSelection
    );
  }


  renderer.domElement.addEventListener(
    "pointermove",
    handlePointerMove
  );

  renderer.domElement.addEventListener(
    "pointerleave",
    handlePointerLeave
  );

  renderer.domElement.addEventListener(
    "click",
    handlePointerClick
  );


  function resize(): void {

    const width =
      Math.max(
        container.clientWidth,
        1
      );

    const height =
      Math.max(
        container.clientHeight,
        1
      );

    renderer.setSize(
      width,
      height,
      false
    );

    labelRenderer.setSize(
      width,
      height
    );

    camera.aspect =
      width / height;

    camera.updateProjectionMatrix();
  }


  const resizeObserver =
    new ResizeObserver(
      resize
    );

  resizeObserver.observe(
    container
  );

  resize();


  let animationFrame =
    0;


  const animate =
    (): void => {

      animationFrame =
        requestAnimationFrame(
          animate
        );

      controls.update();

      renderer.render(
        scene,
        camera
      );

      labelRenderer.render(
        scene,
        camera
      );
    };


  animate();


  return {

    load,

    clear,

    fit,

    setVerticesVisible,

    setHoveredVertex,

    setSelectedVertex,

    setVertexHoverHandler,

    setFaceSelectMode,

    setFaceColorsEnabled,

    setHoveredFace,

    setSelectedFace,

    setFaceHoverHandler,

    setVertexSelectionHandler,

    setFaceSelectionHandler,


    dispose(): void {

      cancelAnimationFrame(
        animationFrame
      );

      resizeObserver.disconnect();

      renderer.domElement.removeEventListener(
        "pointermove",
        handlePointerMove
      );

      renderer.domElement.removeEventListener(
        "pointerleave",
        handlePointerLeave
      );

      renderer.domElement.removeEventListener(
        "click",
        handlePointerClick
      );

      clear();

      controls.dispose();

      renderer.dispose();

      renderer.domElement.remove();

      labelRenderer.domElement.remove();
    }
  };
}


export function boundingBoxText(
  vertices: Vec3[]
): string {

  if (
    vertices.length === 0
  ) {
    return "-";
  }

  let minX = Infinity;
  let minY = Infinity;
  let minZ = Infinity;

  let maxX = -Infinity;
  let maxY = -Infinity;
  let maxZ = -Infinity;

  for (
    const vertex of vertices
  ) {

    minX =
      Math.min(
        minX,
        vertex.x
      );

    minY =
      Math.min(
        minY,
        vertex.y
      );

    minZ =
      Math.min(
        minZ,
        vertex.z
      );

    maxX =
      Math.max(
        maxX,
        vertex.x
      );

    maxY =
      Math.max(
        maxY,
        vertex.y
      );

    maxZ =
      Math.max(
        maxZ,
        vertex.z
      );
  }

  return (
    `X ${minX}..${maxX}` +
    ` | Y ${minY}..${maxY}` +
    ` | Z ${minZ}..${maxZ} mm`
  );
}