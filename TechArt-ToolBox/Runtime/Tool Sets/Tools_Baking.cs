using System.Collections.Generic;

using UnityEngine;

namespace VRR.TechArtTools.BakingTools
{
    public class Tools_Baking : ScriptableObject
    {
        #region EDITOR SET PROPERTIES
        #pragma warning disable 0649
        [SerializeField] private TextureBaker   _baker;
        [SerializeField] private string         _groupName      = "Test Group";
        [SerializeField] private int            _atlasDivision  = 4;
        [SerializeField] private string         _shaderName;
        [SerializeField] private bool           _willGenerateMaterials;

        [SerializeField] private Texture2D      _redChannel;
        [SerializeField] private Texture2D      _greenChannel;
        [SerializeField] private Texture2D      _blueChannel;
        [SerializeField] private Texture2D      _alphaChannel;
        #pragma warning restore 0649
        #endregion

        public void SetBaker(TextureBaker baker)
        {
            _baker = baker;
        }

        public void GenerateBakeGroup()
        {
            BakeGroup bakerGroup;

            if (_baker == null)
            {
                GenerateBakerParent();
            }

            GameObject group = GameObject.Find(_groupName); 

            if(group != null)
            {
                BakeGroup bakeGroup = group.GetComponent<BakeGroup>();
                if (bakeGroup != null)
                {
                    _baker.BakeGroups.Remove(bakeGroup);
                }

                DestroyImmediate(group);
            }

            group = new GameObject(_groupName);
            group.transform.SetParent(_baker.transform);
            group.transform.localPosition = new Vector3(0.0f, 0.0f, 0.25f);

            bakerGroup = group.AddComponent<BakeGroup>();
            _baker.BakeGroups.Add(bakerGroup);

            GenerateQuads(group, _atlasDivision);
        }

        private void GenerateBakerParent()
        {
            string bakerName = "Runtime Texture Baker";

            TextureBaker baker = GameObject.FindObjectOfType<TextureBaker>();

            if (baker != null)
            {
                _baker = baker;
            }
            else
            {
                GameObject bakerParent = new GameObject(bakerName);
                _baker                 = bakerParent.AddComponent<TextureBaker>();
                _baker.BakeGroups      = new List<BakeGroup>();
            }
        }

        private void GenerateQuads(GameObject group, int atlasDivision)
        {
            GameObject[] quads = new GameObject[atlasDivision * atlasDivision];

            float scale = 1 / (atlasDivision * 1.0f);

            Vector3 scaleFactor = new Vector3(scale, scale, 1);
            Vector2 startPos    = new Vector2(scale / 2, scale / 2);

            for (int i = 0; i < quads.Length; i++)
            {
                int column = Mathf.RoundToInt(i % atlasDivision);
                int row = Mathf.RoundToInt(Mathf.Floor(i / atlasDivision));

                Vector2 offset = new Vector2(column, row) * scaleFactor;
                offset = new Vector2(offset.x + startPos.x - 0.5f, offset.y + startPos.y - 0.5f);

                quads[i] = GameObject.CreatePrimitive(PrimitiveType.Quad);
                quads[i].name = "Atlas [" + i + "]";
                quads[i].transform.SetParent(group.transform);
                quads[i].transform.localScale = scaleFactor;
                quads[i].transform.localPosition = offset;

                if (_willGenerateMaterials)
                {
                    Renderer renderer = quads[i].gameObject.GetComponent<Renderer>();
                    renderer.material = GenerateMaterials(group, i);
                }
            }
        }

        /// <summary>
        /// In cases where you have a special mesh with unique UVs instead of the simple Quad
        /// </summary>
        /// <param name="group"></param>
        /// <param name="atlasDivision"></param>
        /// <param name="specialObj"></param>
        private void GenerateQuads(GameObject group, int atlasDivision, GameObject specialObj)
        {
            GameObject[] quads = new GameObject[atlasDivision ^ 2];

            for (int i = 0; i < quads.Length; i++)
            {
                int column = Mathf.RoundToInt(i % atlasDivision);
                int row = Mathf.RoundToInt(Mathf.Floor(i / atlasDivision));

                Vector2 offset = new Vector2(column, row);

                quads[i] = Instantiate(specialObj);
                quads[i].name = "Atlas [" + i + "]";
                quads[i].transform.SetParent(group.transform);
                quads[i].transform.localPosition = offset;
            }
        }

        private Material GenerateMaterials(GameObject group, int index)
        {
            Material newMaterial = new Material(Shader.Find(_shaderName));
            newMaterial.name = group.name + " - Source [" + index + "]";

            return newMaterial;
        }

        public void Bake()
        {
            if (_baker != null)
            {
                _baker.BakeAllGroups();
            }
        }

        public void PackTexture()
        {

        }
    }
}