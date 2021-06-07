using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.TechArtTools.Utilities
{
    public class CustomUtilities 
    {
        public static int Contains(ArrayList searchList, string searchName)
        {
            for (int i = 0; i < searchList.Count; i++)
            {
                if (((Material)searchList[i]).name == searchName)
                {
                    return i;
                }
            }
            return -1;
        }

        public static string[] GetBlendshapeNames(SkinnedMeshRenderer mesh)
        {
            string[] blendshapes = new string[mesh.sharedMesh.blendShapeCount];

            for (int i = 0; i < blendshapes.Length; i++)
            {
                blendshapes[i] = mesh.sharedMesh.GetBlendShapeName(i);
            }
            return blendshapes;
        }

        public static List<T> GetListFromEnum<T>()
        {
            List<T> enumList = new List<T>();
            System.Array enums = System.Enum.GetValues(typeof(T));

            foreach (T e in enums)
            {
                enumList.Add(e);
            }

            return enumList;
        }

        public static Vector3 CalculateVelocity(Vector3 current, Vector3 previous)
        {
            return (current - previous) / Time.deltaTime;
        }

        public static Vector3 CalculateAngularVelocity(Quaternion current, Quaternion previous)
        {
            Quaternion deltaRotation = current * Quaternion.Inverse(previous);

            deltaRotation.ToAngleAxis(out float angle, out Vector3 axis);

            angle *= Mathf.Deg2Rad;

            return axis * angle * (1.0f / Time.deltaTime);
        }
    }
}