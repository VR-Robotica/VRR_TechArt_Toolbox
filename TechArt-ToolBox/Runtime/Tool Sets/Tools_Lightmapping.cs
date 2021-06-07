using System.Collections;
using System.Collections.Generic;
using System.IO;

using UnityEngine;

namespace VRR.TechArtTools.Lightmapping
{
    public class Tools_Lightmapping : ScriptableObject
    {
        #region EDITOR SET PROPERTIES
        #pragma warning disable 0414
        [SerializeField] private string _savePath = "Lightmaps/";
        [SerializeField] private string _fileName = "Lightmap";
        [SerializeField] private Texture2D[] _lightmaps;
        #pragma warning restore 0414
        #endregion


        /// <summary>
        /// Static Getters are general utilities that pull lightmap information from the scene's lighting asset
        /// </summary>
        #region STATIC GETTERS
        public static int GetLightmapIndex(Renderer renderer)
        {
            return renderer.lightmapIndex;
        }

        public static Vector2 GetLightmapPosition(Renderer renderer)
        {
            if (renderer == null)
            {
                return Vector2.zero;
            }

            Vector4 scaleAndOffset = renderer.lightmapScaleOffset;

            Vector2 position = new Vector2(scaleAndOffset.z, scaleAndOffset.w);

            return position;
        }

        public static Vector2 GetLightmapScale(Renderer renderer)
        {
            if (renderer == null)
            {
                return Vector2.zero;
            }

            Vector4 scaleAndOffset = renderer.lightmapScaleOffset;

            Vector2 scale = new Vector2(scaleAndOffset.x, scaleAndOffset.y);

            return scale;
        }

        public static Texture2D GetLightmapTexture(int index)
        {
            LightmapData[] lightmapData = LightmapSettings.lightmaps;

            Debug.Log("Source Image: " + lightmapData[index].lightmapColor.name);

            return lightmapData[index].lightmapColor;
        }

        public static Texture2D[] GetLightmapTextures()
        {
            LightmapData[] lightmapData = LightmapSettings.lightmaps;

            Texture2D[] lightmaps = new Texture2D[lightmapData.Length];

            for (int i = 0; i < lightmaps.Length; i++)
            {
                lightmaps[i] = lightmapData[i].lightmapColor;
            }

            return lightmaps;
        }
        #endregion

        /// <summary>
        /// The is core function of the ToolBox Editor
        /// </summary>
        /// <param name="folder"></param>
        /// <param name="name"></param>
        public void CopyLightmapTexture(string folder, string name)
        {
            Texture2D[] lightmaps = GetLightmapTextures();
            
            int index = 0;

            foreach (Texture2D map in lightmaps)
            {
                // step 1: create texture base
                Texture2D texture = new Texture2D(map.width, map.height, map.format, true, true);

                // step 2: copy lightmap texture into new texture
                Graphics.CopyTexture(map, texture);

                //Graphics.ConvertTexture(sourceTexture, texture);

                // step 3: encode image as PNG
                byte[] bytes = texture.EncodeToPNG();
                // byte[] bytes = texture.EncodeToEXR(Texture2D.EXRFlags.OutputAsFloat);

                // step 4: set up directory path
                string dirPath = Application.dataPath + "/" + folder;
                //string dirPath = AssetDatabase.GetAssetPath(sourceTexture);

                if (!Directory.Exists(dirPath))
                {
                    Directory.CreateDirectory(dirPath);
                }

                // step 5: save to disk!
                File.WriteAllBytes(dirPath + "/" + name + "_" + index + ".png", bytes);

                DestroyImmediate(texture);

                index++;
            }
        }
    }
}