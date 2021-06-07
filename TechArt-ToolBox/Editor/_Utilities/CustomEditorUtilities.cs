using System.Collections.Generic;

using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace VRR.TechArtTools.Utilities
{
    public static class CustomEditorUtilities
    {
        public static string[] GetAllShaderNames()
        {
            List<string> names = new List<string>();

            ShaderInfo[] shaderInfo = ShaderUtil.GetAllShaderInfo(); 

            foreach(ShaderInfo si in shaderInfo)
            {
                names.Add(si.name);
            }

            return names.ToArray();
        }

        public static void AddComponents(GameObject go, MonoScript[] components)
        {
            if (components != null && components.Length > 0)
            {
                for (int i = 0; i < components.Length; i++)
                {
                    go.AddComponent(components[i].GetClass());
                }
            }
        }

        public static string GetPath(GameObject source)
        {
            int nameLength = source.name.ToCharArray().Length;
            string path = AssetDatabase.GetAssetPath(source);

            // remove file name from path string
            path = path.Remove(path.ToCharArray().Length - (nameLength + 4)); // 4 charaacters for the extension (eg .fbx )
            //path.Replace(fileName, "");

            return path;
        }

        public static GUIStyle GoodTextStyle()
        {
            GUIStyle goodText = new GUIStyle(EditorStyles.boldLabel);
            goodText.normal.textColor = Color.green;

            return goodText;
        }

        public static GUIStyle MedTextStyle()
        {
            GUIStyle medText = new GUIStyle(EditorStyles.boldLabel);
            medText.normal.textColor = Color.cyan;

            return medText;
        }

        public static GUIStyle BadTextStyle()
        {
            GUIStyle badText = new GUIStyle(EditorStyles.boldLabel);
            badText.normal.textColor = Color.red;

            return badText;
        }

        public static string[] GetBlendshapeNames(SkinnedMeshRenderer mesh)
        {
            string[] blendshapes = new string[mesh.sharedMesh.blendShapeCount + 1];

            for (int i = 0; i < blendshapes.Length - 1; i++)
            {
                blendshapes[i] = mesh.sharedMesh.GetBlendShapeName(i);
            }

            blendshapes[blendshapes.Length - 1] = "NONE";

            return blendshapes;
        }

        public static string[] GetParameterNames(AnimatorController anim)
        {
            string[] parameters = new string[anim.parameters.Length];

            for (int i = 0; i < parameters.Length; i++)
            {
                parameters[i] = anim.parameters[i].name;
            }

            return parameters;
        }
    }
}
