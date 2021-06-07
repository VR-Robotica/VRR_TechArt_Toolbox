using System.IO;
using UnityEditor;
using UnityEngine;

using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Utilities;

namespace VRR.TechArtTools.Lightmapping
{
    public class MenuTab_LightmapTools : IMenuTab
    {
        Tools_Lightmapping LightmapToolScript;
        protected SerializedObject so;

        SerializedProperty LightmapPath;
        SerializedProperty LightmapName;
        SerializedProperty Lightmaps;

        private string     _resultsPath;
        private GUIStyle   _resultsStyle;

        public MenuTab_LightmapTools()
        {
            GetProperties();
        }

        #region INTERFACE FUNCTIONS
        public void Update()
        {
            if (so.targetObject == null)
            {
                GetProperties();
            }

            so.Update();
        }

        public void Apply()
        {
            so.ApplyModifiedProperties();
        }


        public void GetProperties()
        {
            LightmapToolScript = ScriptableObject.CreateInstance<Tools_Lightmapping>();
            so = new SerializedObject(LightmapToolScript);

            LightmapPath = so.FindProperty("_savePath");
            LightmapName = so.FindProperty("_fileName");
            Lightmaps = so.FindProperty("_lightmaps");
        }

        public void DrawMenu(GUIStyle style)
        {
            DrawImages_Lightmaps(style);
            DrawMenu_SaveLightmap(style);
        }
        #endregion

        #region MENU CALLS
        private void DrawMenu_SaveLightmap(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Save Lightmap as PNG", style);

            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(LightmapPath);
            EditorGUILayout.PropertyField(LightmapName);

            EditorGUILayout.Space();

            DrawResultsPath(Application.dataPath + "/" + LightmapPath.stringValue + LightmapName.stringValue + ".png");

            EditorGUILayout.Space();

            DrawButton_Save();

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }

        private void DrawResultsPath(string resultPath)
        {
            GUIStyle resultStyle;

            if (Directory.Exists(resultPath))
            {
                resultStyle = CustomEditorUtilities.GoodTextStyle();
            }
            else
            {
                resultStyle = CustomEditorUtilities.MedTextStyle();
            }

            EditorGUILayout.LabelField("Target Directory:", EditorStyles.boldLabel);
            EditorGUILayout.LabelField(resultPath, resultStyle);
        }

        private void DrawButton_Save()
        {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Save Lightmap"))
            {
                LightmapToolScript.CopyLightmapTexture(LightmapPath.stringValue, LightmapName.stringValue);
            }

            EditorGUILayout.EndHorizontal();
        }

        private void DrawImages_Lightmaps(GUIStyle style)
        {
            Texture2D[] maps = Tools_Lightmapping.GetLightmapTextures();
            int size = 100;
            int space = 15;

            EditorGUILayout.BeginVertical("Box");


            for (int i = 0; i < maps.Length; i++)
            {
                EditorGUI.PrefixLabel(new Rect(25, 45 + (i * space), size, 25), 0, new GUIContent("Lightmap_" + i));
                EditorGUI.DrawPreviewTexture(new Rect(25, 60 + (i * space), size, size), maps[i]);
            }

            EditorGUILayout.Space(200);

            EditorGUILayout.EndVertical();
        }
        #endregion
    }
}