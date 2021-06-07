using System.Collections.Generic;

using UnityEngine;

namespace VRR.TechArtTools.GeneralTools
{
    public class Tools_General : ScriptableObject
    {
        enum MatchOrientations { RotationsOnly, LocalPositions, WorldPositions }

        #region EDITOR SET PROPERTIES
        #pragma warning disable 0649
        [SerializeField] private GameObject     _sourceGroup;
        [SerializeField] private GameObject[]   _sourceObjects;
        
        [SerializeField] private List<Material> _targetMaterials;

        [SerializeField] MatchOrientations _matchOrientation = MatchOrientations.RotationsOnly;
        #pragma warning restore 0649
        #endregion

        private void OnEnable()
        {
            SetDefaultValues();
        }


        #region SCENE STATS
        public Dictionary<GameObject, int> SceneCount = new Dictionary<GameObject, int>();


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
                        Renderer renderer = allMeshFilters[i].gameObject.GetComponent<Renderer>();

                        if (renderer != null)
                        {
                            foreach (var mat in GetMaterials(renderer))
                            {
                                if (mat != null && !materialDict.ContainsKey(mat))
                                {
                                    materialDict.Add(mat, mat.passCount);
                                }
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
                            if (mat != null && !materialDict.ContainsKey(mat))
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


        public void Clear()
        {
            _sourceObjects = null;
        }
        #endregion
        #endregion

        #region FOLDER SETUP
        [SerializeField] private string[] _subFolders;

        public string[] DefaultPathNames;

        public void SetDefaultValues()
        {
            DefaultPathNames = new string[] { "_Prefabs", "Animations", "Audio", "Materials", "Models", "Textures" };

            _subFolders = DefaultPathNames;
        }
        #endregion

        #region POSE MATCH

        public void MatchPose(Transform sourceRoot, Transform targetRoot)
        {
            Transform[] allSourceTransforms = sourceRoot.GetComponentsInChildren<Transform>();
            Transform[] allTargetTransforms = targetRoot.GetComponentsInChildren<Transform>();

            if(sourceRoot.childCount != targetRoot.childCount) { Debug.Log("<color=red>Mismatched child count!</color>"); return; }


            for (int i = 0; i < allTargetTransforms.Length; i++)
            {
                if (allTargetTransforms[i].name == allSourceTransforms[i].name)
                {
                    switch (_matchOrientation)
                    {

                        case MatchOrientations.RotationsOnly:
                            sourceRoot.localRotation = targetRoot.localRotation;
                            allSourceTransforms[i].localRotation = allTargetTransforms[i].localRotation;
                            break;

                        case MatchOrientations.LocalPositions:
                            sourceRoot.localPosition = targetRoot.localPosition;
                            sourceRoot.localRotation = targetRoot.localRotation;
                            allSourceTransforms[i].localPosition = allTargetTransforms[i].localPosition;
                            allSourceTransforms[i].localRotation = allTargetTransforms[i].localRotation;
                            break;

                        case MatchOrientations.WorldPositions:
                            sourceRoot.position = targetRoot.position;
                            sourceRoot.rotation = targetRoot.rotation;
                            allSourceTransforms[i].position = allTargetTransforms[i].position;
                            allSourceTransforms[i].rotation = allTargetTransforms[i].rotation;
                            break;
                    }
                }
            }
        }

        #endregion

    }
}