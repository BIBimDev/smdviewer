import * as THREE from "three";

import {
  OrbitControls
} from
  "three/examples/jsm/controls/OrbitControls.js";

import type {
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

  dispose(): void;
}


function buildGeometry(
  parsed: ParsedSmd
): THREE.BufferGeometry {

  const vertices =
    parsed.geometry.brep.verticesWorld;

  const positions: number[] = [];


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
        const vertexIndex
        of triangle
      ) {

        const v =
          vertices[vertexIndex];

        positions.push(
          v.x,
          v.y,
          v.z
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


function makeGrid(
  size: number
): THREE.GridHelper {

  const safeSize =
    Math.max(
      size,
      1000
    );

  const divisions = 20;


  const grid =
    new THREE.GridHelper(
      safeSize,
      divisions
    );


  // Three.js GridHelper lies on XZ.
  // STRAKON uses Z as vertical.
  // Therefore rotate the grid to XY.

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


  const controls =
    new OrbitControls(
      camera,
      renderer.domElement
    );


  controls.enableDamping = true;

  controls.dampingFactor = 0.08;


  const ambient =
    new THREE.AmbientLight(
      0xffffff,
      1.8
    );


  scene.add(ambient);


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


  scene.add(keyLight);


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


  scene.add(fillLight);


  const axes =
    new THREE.AxesHelper(
      1000
    );


  scene.add(axes);


  let modelRoot:
    THREE.Group | undefined;


  let vertexRoot:
    THREE.Group | undefined;


  let grid:
    THREE.GridHelper | undefined;


  let verticesVisible = false;


  function clearVertexRoot(): void {

    if (!vertexRoot) {
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
          object
          instanceof THREE.Mesh
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
              ) =>
                material.dispose()
            );

          } else {

            object.material.dispose();

          }

        }

      }
    );


    vertexRoot = undefined;
  }


  function clear(): void {

    if (modelRoot) {

      scene.remove(
        modelRoot
      );


      modelRoot.traverse(
        (
          object:
          THREE.Object3D
        ) => {

          if (
            object
              instanceof THREE.Mesh
            ||
            object
              instanceof THREE.LineSegments
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
                ) =>
                  material.dispose()
              );

            } else {

              object.material.dispose();

            }

          }

        }
      );


      modelRoot = undefined;
    }


    clearVertexRoot();


    if (grid) {

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
          ) =>
            material.dispose()
        );

      } else {

        grid.material.dispose();

      }


      grid = undefined;
    }

  }


  function fit(): void {

    if (!modelRoot) {
      return;
    }


    const box =
      new THREE.Box3()
        .setFromObject(
          modelRoot
        );


    if (box.isEmpty()) {
      return;
    }


    const size =
      new THREE.Vector3();


    const center =
      new THREE.Vector3();


    box.getSize(size);

    box.getCenter(center);


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
        + distance * 0.8,

      center.y
        - distance * 0.9,

      center.z
        + distance * 0.65

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


    root.visible =
      verticesVisible;


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


      const geometry =
        new THREE.SphereGeometry(
          radius,
          12,
          8
        );


      const material =
        new THREE.MeshBasicMaterial({
          color: 0x2563eb
        });


      const node =
        new THREE.Mesh(
          geometry,
          material
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


      root.add(node);
    }


    vertexRoot = root;


    scene.add(root);
  }


  function load(
    parsed: ParsedSmd
  ): void {

    clear();


    const geometry =
      buildGeometry(parsed);


    const root =
      new THREE.Group();


    const material =
      new THREE.MeshStandardMaterial({

        color: 0xc9cdd2,

        roughness: 0.82,

        metalness: 0,

        side:
          THREE.DoubleSide

      });


    const mesh =
      new THREE.Mesh(
        geometry,
        material
      );


    root.add(mesh);


    const edgeGeometry =
      new THREE.EdgesGeometry(
        geometry,
        1
      );


    const edgeMaterial =
      new THREE.LineBasicMaterial({
        color: 0x222222
      });


    const edges =
      new THREE.LineSegments(
        edgeGeometry,
        edgeMaterial
      );


    root.add(edges);


    modelRoot = root;


    scene.add(root);


    // Day 1:
    // Create one visible/debug node
    // for every final/world BREP vertex.

    buildVertexNodes(parsed);


    const box =
      new THREE.Box3()
        .setFromObject(
          root
        );


    const size =
      new THREE.Vector3();


    box.getSize(size);


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


    scene.add(grid);


    fit();
  }


  function setVerticesVisible(
    visible: boolean
  ): void {

    verticesVisible =
      visible;


    if (vertexRoot) {

      vertexRoot.visible =
        visible;

    }

  }


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


  let animationFrame = 0;


  const animate = (): void => {

    animationFrame =
      requestAnimationFrame(
        animate
      );


    controls.update();


    renderer.render(
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


    dispose(): void {

      cancelAnimationFrame(
        animationFrame
      );


      resizeObserver.disconnect();


      clear();


      controls.dispose();


      renderer.dispose();


      renderer.domElement.remove();

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
    const v of vertices
  ) {

    minX =
      Math.min(
        minX,
        v.x
      );

    minY =
      Math.min(
        minY,
        v.y
      );

    minZ =
      Math.min(
        minZ,
        v.z
      );


    maxX =
      Math.max(
        maxX,
        v.x
      );

    maxY =
      Math.max(
        maxY,
        v.y
      );

    maxZ =
      Math.max(
        maxZ,
        v.z
      );

  }


  return (
    `X ${minX}..${maxX}` +
    ` | Y ${minY}..${maxY}` +
    ` | Z ${minZ}..${maxZ} mm`
  );
}