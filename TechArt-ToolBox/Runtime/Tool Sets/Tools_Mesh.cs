using System.Collections;
using System.Collections.Generic;
using System.IO;

using UnityEngine;

using VRR.TechArtTools.Lightmapping;
using VRR.TechArtTools.Utilities;

namespace VRR.TechArtTools.MeshTools
{
    public class Tools_Mesh : ScriptableObject
    {
        #region EDITOR SET PROPERTIES
        #pragma warning disable 0649
        [SerializeField] private GameObject     _sourceGroup;
        [SerializeField] private GameObject[]   _sourceObjects;
        
        [SerializeField] private List<Material> _targetMaterials;
        [SerializeField] private string         _prefix     = "LM_";
        [SerializeField] private UVSet          _uvSet      = UVSet.uv1;
        [SerializeField] private bool           _willModifyLightmapUVs = true;

        [SerializeField] private string         _combineParentName;
        [SerializeField] private GameObject     _combineGroup;
        [SerializeField] private bool           _destroyOldMesh;

        [SerializeField] private GameObject     _exportObject;
        [SerializeField] private string         _exportPath = "Export";
        [SerializeField] private string         _exportName = "TestModel";

        // cache properties so you can save a prefab
        [SerializeField] private Mesh[]         _duplicatedMeshes;
        [SerializeField] private Mesh           _combinedMesh;
        #pragma warning restore 0649
        #endregion

        private List<Material>  _sourceMaterials;
        private Renderer[]      _cachedRenderers;
        private string          _resultPath;

        public Dictionary<GameObject, int> SceneCount = new Dictionary<GameObject, int>();

        #region GETTERS

        private List<Material> GetMaterials(Renderer[] renderers)
        {
            List<Material> materials = new List<Material>();

            foreach (Renderer renderer in renderers)
            {
                if (!materials.Contains(renderer.sharedMaterial))
                {
                    materials.Add(renderer.sharedMaterial);
                }
            }

            return materials;
        }

        private Material GetRelatedMaterialByName(string sourceName, List<Material> targetMaterials)
        {
            for (int i = 0; i < targetMaterials.Count; i++)
            {
                if (targetMaterials[i].name == _prefix + sourceName)
                {
                    return targetMaterials[i];
                }
            }

            return null;
        }

        public Renderer[] GetSourceGroupRenderers()
        {
            List<Renderer> rend = new List<Renderer>();

            if (_sourceGroup != null)
            {
                Renderer topRenderer = _sourceGroup.GetComponent<Renderer>();

                Renderer[] renderers = _sourceGroup.GetComponentsInChildren<Renderer>();

                if (topRenderer != null)
                {
                    //rend.Add(topRenderer);
                }

                if (renderers != null && renderers.Length > 0)
                {
                    for (int i = 0; i < renderers.Length; i++)
                    {
                        rend.Add(renderers[i]);
                    }
                }

                return rend.ToArray();
            }

            return null;
        }

        #endregion

        #region SETTERS
        private void Set_TargetMaterial(int index, Renderer renderer)
        {
            renderer.sharedMaterials[index] = _targetMaterials[index];
        }

        private void Set_Materials(Renderer renderer)
        {
            // Step 1: change the settings
            SetRendererSettings(renderer);

            //  Step 2: validate our target materials
            if (_targetMaterials == null || _targetMaterials.Count == 0)
            {
                Debug.LogWarning("Material not set in editor window");
                return;
            }

            //  Step 3: cache a reference to the original renderers
            Material[] sourceMaterials = renderer.sharedMaterials;

            //  Step 4: make an array of materials the same length as the one on the model
            Material[] targetMaterials = new Material[sourceMaterials.Length];

            for (int i = 0; i < targetMaterials.Length; i++)
            {
                if (sourceMaterials[i] != null)
                {
                    //  Step 5b: compare names and if related material exists, get related material
                    if (GetRelatedMaterialByName(sourceMaterials[i].name, _targetMaterials) != null)
                    {
                        targetMaterials[i] = GetRelatedMaterialByName(sourceMaterials[i].name, _targetMaterials);
                    }
                    else
                    {
                        Debug.LogWarning("No related material found: [" + _prefix + sourceMaterials[i].name + "] - Defaulting to orginal material");
                        targetMaterials[i] = sourceMaterials[i];
                    }
                }
                else
                {
                    Debug.LogWarning("No Source Material at [" + i + "] Found");
                }
            }

            //  Step 6: apply our final material array to the object
            renderer.sharedMaterials = targetMaterials;
        }

        private void SetRendererSettings(Renderer renderer)
        {
            // Step 1: turn off unused MeshRenderer Settings
            renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            renderer.rayTracingMode = UnityEngine.Experimental.Rendering.RayTracingMode.Off;
            renderer.lightProbeUsage = UnityEngine.Rendering.LightProbeUsage.Off;
            renderer.reflectionProbeUsage = UnityEngine.Rendering.ReflectionProbeUsage.Simple;
            renderer.receiveShadows = false;
            //renderer.lightmapScaleOffset  = new Vector4(1, 1, 0, 0);
            //renderer.realtimeLightmapScaleOffset = new Vector4(1, 1, 0, 0);
        }
        #endregion

        #region CORE FUNCTIONS
        public Dictionary<GameObject, int> GetListOfObjectsVertCounts(MeshFilter[] allMeshFilters, SkinnedMeshRenderer[] allSkinnedMeshes)
        {
            Dictionary<GameObject, int> vertDict = new Dictionary<GameObject, int>();

            if (allMeshFilters != null)
            {
                for (int i = 0; i < allMeshFilters.Length; i++)
                {
                    Mesh sharedMesh = allMeshFilters[i].sharedMesh;

                    if (sharedMesh != null && allMeshFilters[i].gameObject.activeInHierarchy)
                    {
                        if (allMeshFilters[i].sharedMesh.vertexCount > 0)
                        {
                            vertDict.Add(allMeshFilters[i].gameObject, GetVertCount(allMeshFilters[i].sharedMesh));
                        }
                    }
                }
            }

            if (allSkinnedMeshes != null)
            {
                for (int i = 0; i < allSkinnedMeshes.Length; i++)
                {
                    Mesh sharedMesh = allSkinnedMeshes[i].sharedMesh;

                    if (sharedMesh != null && allSkinnedMeshes[i].gameObject.activeInHierarchy)
                    {
                        if (allSkinnedMeshes[i].sharedMesh.vertexCount > 0)
                        {
                            vertDict.Add(allSkinnedMeshes[i].gameObject, GetVertCount(allSkinnedMeshes[i].sharedMesh));
                        }
                    }
                }
            }

            return vertDict;
        }

        public Dictionary<Material, int> GetListOfMaterialPassCount(MeshFilter[] allMeshFilters, SkinnedMeshRenderer[] allSkinnedMeshes)
        {
            Dictionary<Material, int> materialDict = new Dictionary<Material, int>();

            if (allMeshFilters != null)
            { 
                for (int i = 0; i < allMeshFilters.Length; i++)
                {
                    Mesh sharedMesh = allMeshFilters[i].sharedMesh;

                    if (sharedMesh != null && allMeshFilters[i].gameObject.activeInHierarchy)
                    {
                        foreach (var mat in GetMaterials(allMeshFilters[i].gameObject.GetComponent<Renderer>()))
                        {
                            if (!materialDict.ContainsKey(mat))
                            {
                                materialDict.Add(mat, mat.passCount);
                            }
                        }
                    }
                }
            }

            if (allSkinnedMeshes != null)
            {
                for (int i = 0; i < allSkinnedMeshes.Length; i++)
                {
                    Mesh sharedMesh = allSkinnedMeshes[i].sharedMesh;

                    if (sharedMesh != null && allSkinnedMeshes[i].gameObject.activeInHierarchy)
                    {
                        foreach (var mat in GetMaterials(allSkinnedMeshes[i].gameObject.GetComponent<Renderer>()))
                        {
                            if (!materialDict.ContainsKey(mat))
                            {
                                materialDict.Add(mat, mat.passCount);
                            }
                        }
                    }
                }
            }

            return materialDict;
        }
        
        public Vector3Int GetTotalVertsAndTriCount(MeshFilter[] allMeshFilters, SkinnedMeshRenderer[] allSkinnedMeshes)
        {
            int vertexCount = 0;
            int triangleCount = 0;

            int meshVertCount   = 0;
            int skinVertCount   = 0;

            int meshTriCount    = 0;
            int skinTriCount    = 0;

            int meshFilterAmount  = 0;
            int skinnedMeshAmount = 0;


            vertexCount += meshVertCount + skinVertCount;
            triangleCount += meshTriCount + skinTriCount;

            if (allMeshFilters == null)
            {
                 meshFilterAmount = 0;
            }
            else
            {
                for (int i = 0; i < allMeshFilters.Length; i++)
                {
                    Mesh sharedMesh = allMeshFilters[i].sharedMesh;

                    if (sharedMesh != null && allMeshFilters[i].gameObject.activeInHierarchy)
                    {
                        vertexCount += GetVertCount(sharedMesh);
                        triangleCount += GetTriCount(sharedMesh);
                    }
                }

                meshFilterAmount = allMeshFilters.Length;
            }

            if (allSkinnedMeshes == null)
            {
                skinnedMeshAmount = 0;
            }
            else
            {
                for (int i = 0; i < allSkinnedMeshes.Length; i++)
                {
                    Mesh sharedMesh = allSkinnedMeshes[i].sharedMesh;

                    if (sharedMesh != null && allSkinnedMeshes[i].gameObject.activeInHierarchy)
                    {
                        vertexCount += GetVertCount(sharedMesh);
                        triangleCount += GetTriCount(sharedMesh);
                    }
                }

                skinnedMeshAmount = allSkinnedMeshes.Length;
            }

            Vector3Int finalCount = new Vector3Int(vertexCount, triangleCount, meshFilterAmount + skinnedMeshAmount);

            return finalCount;
        }

        public int GetVertCount(Mesh mesh)
        {
            return mesh.vertexCount;
        }

        public int GetTriCount(Mesh mesh)
        {
            return mesh.triangles.Length / 3;
        }


        
        public Material[] GetMaterials(Renderer renderer)
        {
            Material[] materials = renderer.sharedMaterials;

            return materials;
        }

        public int TotalMaterialPassCount(Dictionary<Material, int> dict)
        {
            int total = 0;

            foreach (var kvp in dict)
            {
                total += kvp.Value;
            }

            return total;
        }


        public void DuplicateGroup(GameObject sourceGroup, string versionSuffix)
        {
            // Step 1: create our parent object and duplicate our source
            GameObject ParentGroup = Instantiate(sourceGroup);

            // Step 2: rename parent object to show it is a new version
            ParentGroup.name = sourceGroup.name + versionSuffix;

            // Step 3: cache reference to our original renderers
            Renderer[] originalRenderers = sourceGroup.GetComponentsInChildren<Renderer>();

            // Step 4: cache a reference to our new duplicate renderers 
            // NOTE: Duplicates don't hold lightMap data from the originals
            Renderer[] duplicateRenderers = ParentGroup.GetComponentsInChildren<Renderer>();

            _cachedRenderers = duplicateRenderers;

            // Step 5: get materials of the originals
            _sourceMaterials = GetMaterials(originalRenderers);

            if (_willModifyLightmapUVs)
            {
                // Step 6: loop through our duplicate renderers and start adjusting the UVs
                for (int i = 0; i < duplicateRenderers.Length; i++)
                {
                    if (duplicateRenderers[i].gameObject.name == originalRenderers[i].gameObject.name)
                    {
                        // step 6b: get lightmap scale and offset from original renderers
                        // NOTE: these are found in Unity's Lightmap settings in the object's MeshRenderer
                        Vector2 uvScale = Tools_Lightmapping.GetLightmapScale(originalRenderers[i]);
                        Vector2 uvOffset = Tools_Lightmapping.GetLightmapPosition(originalRenderers[i]);

                        // Step 6c: get our current MeshFilter
                        MeshFilter meshFilter = duplicateRenderers[i].gameObject.GetComponent<MeshFilter>();

                        // step 6d: rename our duplicate mesh more apropriately
                        meshFilter.mesh.name = duplicateRenderers[i].gameObject.name + "_Mesh";

                        // Step 6e: use the lightmapped values to adjust our UVs
                        AdjustUVs(meshFilter, (int)_uvSet, uvScale, uvOffset);

                        // Step 6f: apply our related material to the current renderer
                        Set_Materials(duplicateRenderers[i]);
                    }
                    else
                    {
                        Debug.LogWarning("Mismatched Renderer [" + i + "]: " + duplicateRenderers[i].gameObject.name + " != " + originalRenderers[i].gameObject.name);
                    }
                }
            }
            
            _combineGroup = ParentGroup;
            _exportObject = ParentGroup;
        }

        public void DuplicateObject()
        {
            // Step 1: loop though all of our selected GameObjects
            foreach (GameObject sourceObject in _sourceObjects)
            {
                // Step 1: get the source renderer (to obtain Unity's Lightmap Scale & Offset values)
                Renderer sourceRenderer = sourceObject.GetComponent<Renderer>();

                if (sourceRenderer == null)
                {
                    Debug.LogWarning("<color=cyan>"+ sourceObject.name+"</color> - <color=red>No Renderer Found</color>, Skipping!");
                }
                else
                {
                    // Step 2: get lightmap scale and offset from renderer
                    Vector2 uvScale = Tools_Lightmapping.GetLightmapScale(sourceRenderer);
                    Vector2 uvOffset = Tools_Lightmapping.GetLightmapPosition(sourceRenderer);

                    // Step 3: duplicate our source model
                    GameObject targetCopy = Instantiate(sourceObject);

                    // Step 4: rename the duplicate to indicate that it's the lightmap version
                    targetCopy.name = sourceObject.name + " [Lightmapped]";

                    targetCopy.gameObject.SetActive(true);

                    // Step 5: get our target renderer
                    Renderer targetCopyRenderer = targetCopy.GetComponent<Renderer>();

                    // Step 6: apply our custom lightmapped material
                    Set_Materials(targetCopyRenderer);

                    // Step 7: get the mesh filter
                    MeshFilter meshFilter = targetCopy.GetComponent<MeshFilter>();

                    // Step 8: rename our duplicate mesh more apropriately
                    meshFilter.mesh.name = sourceObject.name + "_Mesh";

                    // Step 9: calculate and adjust new UVs
                    AdjustUVs(meshFilter, (int)_uvSet, uvScale, uvOffset);

                    // Step 10: [FOR TESTING] cache a ref of our object to autofill our export function
                    _exportObject = targetCopy;
                }
            }
        }

        public void CombineMeshes(GameObject go)
        {
            GameObject combineParent = new GameObject(_combineParentName);
            if(_combineParentName == string.Empty) { combineParent.name = "[COMBINED] " + go.name; }

            // Step 1: create our new target object
            GameObject combinedObject = new GameObject("MESH");

            // Step 2: add basic components
            MeshFilter combinedFilter = combinedObject.AddComponent<MeshFilter>();
            Renderer combinedRenderer = combinedObject.AddComponent<MeshRenderer>();

            // Step 3: create materials list (this is to organize the sub-meshes)
            ArrayList materials = new ArrayList();

            // Step 4: create our array of combinedInstances
            ArrayList combineInstanceArrays = new ArrayList();

            // Step 5: cache all the mesh filters
            MeshFilter[] meshFilters = go.GetComponentsInChildren<MeshFilter>();

            // Step 6: loop through all the mesh filters picking out the sub-meshes
            foreach (MeshFilter meshFilter in meshFilters)
            {
                // get a renderer reference off our filter
                MeshRenderer meshRenderer = meshFilter.GetComponent<MeshRenderer>();

                // validate the renderer 
                // - go back up the foreach loop for the next filter if we don't pass the check
                if (!meshRenderer || !meshFilter.sharedMesh || meshRenderer.sharedMaterials.Length != meshFilter.sharedMesh.subMeshCount)
                {
                    continue;
                }

                // loop through all the sub meshes
                for (int i = 0; i < meshFilter.sharedMesh.subMeshCount; i++)
                {
                    // get the material related index
                    int materialArrayIndex = CustomUtilities.Contains(materials, meshRenderer.sharedMaterials[i].name);

                    if (materialArrayIndex == -1)
                    {
                        materials.Add(meshRenderer.sharedMaterials[i]);
                        materialArrayIndex = materials.Count - 1;
                    }

                    // create a new array list and add to our combineInstance array
                    combineInstanceArrays.Add(new ArrayList());

                    CombineInstance combineInstance = new CombineInstance();
                    combineInstance.transform = meshRenderer.transform.localToWorldMatrix;
                    combineInstance.subMeshIndex = i;
                    combineInstance.mesh = meshFilter.sharedMesh;

                    (combineInstanceArrays[materialArrayIndex] as ArrayList).Add(combineInstance);
                }
            }

            // Combine by material index into per-material meshes (sub-meshes)
            // also, Create CombineInstance array for next step
            Mesh[] meshes = new Mesh[materials.Count];
            CombineInstance[] combineInstances = new CombineInstance[materials.Count];

            for (int i = 0; i < materials.Count; i++)
            {
                CombineInstance[] combineInstanceArray = (combineInstanceArrays[i] as ArrayList).ToArray(typeof(CombineInstance)) as CombineInstance[];
                meshes[i] = new Mesh();
                meshes[i].CombineMeshes(combineInstanceArray, true, true);

                combineInstances[i] = new CombineInstance();
                combineInstances[i].mesh = meshes[i];
                combineInstances[i].subMeshIndex = 0;
            }

            // Combine into one
            combinedFilter.sharedMesh = new Mesh();
            combinedFilter.sharedMesh.CombineMeshes(combineInstances, false, false);

            // Assign materials
            Material[] materialsArray = materials.ToArray(typeof(Material)) as Material[];
            combinedRenderer.materials = materialsArray;

            _combinedMesh = combinedFilter.sharedMesh;
            _combinedMesh.name = "CombinedMesh_" + go.name;

            if (_destroyOldMesh)
            {
                #if UNITY_EDITOR
                    DestroyImmediate(go);
                #elif UNITY_RUNTIME
                    Destroy(go);
                #endif
            }

            combinedObject.transform.SetParent(combineParent.transform);

            combineParent.gameObject.isStatic = true;
            combinedObject.gameObject.isStatic = true;

            _exportObject = combineParent;
            _exportName = combineParent.name;
        }

        public Mesh GetCombinedMesh()
        {
            return _combinedMesh;
        }

        private void CombineMeshes(Renderer[] renderers)
        {
            if (_cachedRenderers == null)
            {
                Debug.LogWarning("Cache is empty, nothing to combine.");
                return;
            }

            // Step 1: create the combine object and instances array
            GameObject combinedObject = new GameObject("Combined Object");

            MeshFilter combinedFilter = combinedObject.AddComponent<MeshFilter>();

            Renderer combinedRenderer = combinedObject.AddComponent<MeshRenderer>();

            CombineInstance[] combineInstances = new CombineInstance[renderers.Length];

            // Step 2: loop through mesh count adding each mesh to the array of instances
            for (int i = 0; i < renderers.Length; i++)
            {
                // get current renderer
                MeshFilter currentMeshFilter = renderers[i].gameObject.GetComponent<MeshFilter>();

                // cache an instance of the current renderer's mesh in our array
                combineInstances[i].mesh = currentMeshFilter.sharedMesh;

                // cache the trasform position/rotation/scale of the current renderer
                combineInstances[i].transform = renderers[i].transform.localToWorldMatrix;
            }

            // Step 3: create new combined mesh
            combinedFilter.mesh = new Mesh();
            combinedFilter.mesh.name = "Combined_MESH";
            combinedFilter.mesh.CombineMeshes(combineInstances);
            Set_Materials(combinedRenderer);
        }

        private void AdjustUVs(MeshFilter meshFilter, int uvSet, Vector2 scale, Vector2 offset)
        {
            List<Vector2> originalUVs = new List<Vector2>();
            List<Vector2> adjustedUVs = new List<Vector2>();

            // step 1: cache our original UVs from our defined UV Set into our list of Original UVs        
            // NOTE: use mesh to adjust the lightmap uvs of this particular object, not sharedMesh
            meshFilter.mesh.GetUVs(uvSet, originalUVs);

            // step 2: loop through our cached UVs and adjust to scale and offset values
            for (int i = 0; i < originalUVs.Count; ++i)
            {
                Vector2 moddedUv = originalUVs[i] * scale + offset;

                // step 2b: save adjustment into the list
                adjustedUVs.Add(moddedUv);
            }

            // step 3: apply the list of newly adjusted UVs to the mesh filter
            meshFilter.mesh.SetUVs(uvSet, adjustedUVs);

            // step 4: no longer needed, clear the lists
            originalUVs.Clear();
            adjustedUVs.Clear();

            //Debug.Log("+ " + meshFilter.gameObject.name + " - Scale: " + scale.x + "," + scale.y + ", Offset: " + offset.x + "," + offset.y);
        }

        public void Clear()
        {
            _sourceObjects = null;
        }
#endregion
    }
}