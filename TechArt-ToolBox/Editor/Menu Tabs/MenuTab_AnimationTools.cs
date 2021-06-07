using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;

using UnityEditor;
using UnityEngine;


using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Utilities;

namespace VRR.TechArtTools.AnimationTools
{
    public class MenuTab_AnimationTools : MenuTab<Tools_Animation>
	{
        public AnimationClip animClip;
        public Animator      animatorReference;
        public string[]      excludedNames;

        private bool deletePosition = true;
        private bool deleteScale = true;
        private bool deleteCustom = false;
        private string IgnoreParameterName;


        #region CONSTRUCTOR
        public MenuTab_AnimationTools()
        {
            GetProperties();
		}
        #endregion


        #region INTERFACE FUNCTIONS

        public override void GetProperties()
        {
            toolsScript = ScriptableObject.CreateInstance<Tools_Animation>();
            so = new SerializedObject(toolsScript);
        }

		public override void DrawMenu(GUIStyle style)
		{
			EditorGUILayout.Space();
			animClip = (AnimationClip)EditorGUILayout.ObjectField("Animation Clip: ", animClip, typeof(AnimationClip), true);
			EditorGUILayout.Space();

			DrawMenu_RemoveKeys(style);
			DrawMenu_ClipReNamer(style);
		}
		#endregion

		
		#region ANIMATION CLEAN UP
		private void DrawMenu_RemoveKeys(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");
	            EditorGUILayout.LabelField("Remove Scale & Position Parameters", style);
			EditorGUILayout.EndVertical();

			EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("Box");
				EditorGUILayout.LabelField("Choose the type of keyframes you'd like to remove from the clip");

				deletePosition = GUILayout.Toggle(deletePosition, "Scale Keyframes");
				deleteScale = GUILayout.Toggle(deleteScale, "Position Keyframes");

				IgnoreParameterName = EditorGUILayout.TextField("Ignore this: ", IgnoreParameterName);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

			if (animClip != null)
			{
				if (GUILayout.Button("Clean up"))
				{
					CleanClip(deleteScale, deletePosition);

					if (deleteCustom && IgnoreParameterName != null)
					{
						CleanCustomClip(IgnoreParameterName);
					}

					AssetDatabase.SaveAssets();
				}
			}

            EditorGUILayout.Space();
        }

		private void CleanClip(bool scale, bool pos)
        {
            if (animClip != null && scale)
            {
                EditorCurveBinding[] animationCurves = AnimationUtility.GetCurveBindings(animClip);
                for (int i = 0; i < animationCurves.Length; i++)
                {
                    if (animationCurves[i].propertyName.Contains("Scale"))
                    {
                        AnimationUtility.SetEditorCurve(animClip, animationCurves[i], null);
                    }
                }
            }
            if (animClip != null && pos)
            {
                EditorCurveBinding[] animationCurves = AnimationUtility.GetCurveBindings(animClip);
                for (int i = 0; i < animationCurves.Length; i++)
                {
                    if (animationCurves[i].propertyName.Contains("Position") && animationCurves[i].path != "ACTOR/SKELETON/Hips")
                    {
                        AnimationUtility.SetEditorCurve(animClip, animationCurves[i], null);
                    }
                }
            }
        }
		private void CleanCustomClip(string searchString)
        {
            if (animClip != null)
            {
                EditorCurveBinding[] animationCurves = AnimationUtility.GetCurveBindings(animClip);

                for (int i = 0; i < animationCurves.Length; i++)
                {
                    if (animationCurves[i].path.Contains(searchString))
                    {
                        AnimationUtility.SetEditorCurve(animClip, animationCurves[i], null);
                    }
                }
            }
        }
		#endregion


		#region ANIMATION PARAMETER RE-NAMER
		
		private Vector2 _scrollPos = Vector2.zero;

		private void DrawMenu_ClipReNamer(GUIStyle style)
        {
			if(Selection.activeObject is AnimationClip)
            {
				animClip = (AnimationClip)Selection.activeObject;
				_tempPathOverrides = new Dictionary<string, string>();
				GetAnimationClipPathsAndKeys();
			}
            else
            {
				animClip = null;
				if (_tempPathOverrides != null)
				{
					_tempPathOverrides.Clear();
					_tempPathOverrides = null;
				}
			}

            EditorGUILayout.BeginVertical("Box");
				EditorGUILayout.LabelField("Rename Animation Parameters", style);

				EditorGUILayout.Space();
			EditorGUILayout.HelpBox("Drag n Drop your in-scene Animator Reference:", MessageType.Info, true);
			animatorReference = (Animator)EditorGUILayout.ObjectField("Animator Reference: ", animatorReference, typeof(Animator), true);
            EditorGUILayout.EndVertical();

			EditorGUILayout.Space();

			EditorGUILayout.HelpBox("Enter original parameter path & new parameter path to replace:", MessageType.Info, true);
			EditorGUILayout.BeginHorizontal();
				_originalPropertyName = EditorGUILayout.TextField(_originalPropertyName);
				_newPropertyName = EditorGUILayout.TextField(_newPropertyName);

				if (GUILayout.Button("Replace Root"))
				{
					Debug.Log($"O: {_originalPropertyName} N: {_newPropertyName}");
					ReplaceRoot(_originalPropertyName, _newPropertyName);
				}
			EditorGUILayout.EndHorizontal();

			EditorGUILayout.Space();

            if (animClip == null || animatorReference == null)
            {
				GUILayout.Label("Please select an Animation Clip", CustomEditorUtilities.BadTextStyle());
			}
			else
			{
				EditorGUILayout.BeginHorizontal();
					GUILayout.Label("(Count)", GUILayout.Width(60));
					GUILayout.Label("Parameter path:");
					GUILayout.Label("Replace with GameObject:");
				EditorGUILayout.EndHorizontal();

				_scrollPos = GUILayout.BeginScrollView(_scrollPos, GUIStyle.none);

				if (paths != null)
				{
					foreach (string path in pathsKeys)
					{
						Draw_AnimationPaths(path);
					}
				}

				GUILayout.Space(40);
				GUILayout.EndScrollView();
			}

            EditorGUILayout.Space();
        }

		private void Draw_AnimationPaths(string path)
		{
			string newPath = path;
			GameObject obj = FindObjectInRoot(path);
			GameObject newObj;
			ArrayList properties = (ArrayList)paths[path];

			string pathOverride = path;

			if (_tempPathOverrides.ContainsKey(path))
			{
				pathOverride = _tempPathOverrides[path];
			}

			EditorGUILayout.BeginHorizontal();


			// Count
			EditorGUILayout.BeginVertical(GUILayout.Width(60));

			int refCount = 0;

			if (properties != null && properties.Count > 0)
			{
				refCount = properties.Count;
			}

			EditorGUILayout.LabelField(refCount.ToString(), GUILayout.Width(60));
			EditorGUILayout.EndVertical();



			// Paths
			EditorGUILayout.BeginVertical();
			pathOverride = EditorGUILayout.TextField(pathOverride);

			if (pathOverride != path)
			{
				_tempPathOverrides[path] = pathOverride;
			}
			EditorGUILayout.EndVertical();



			// GameObjects
			EditorGUILayout.BeginVertical();
			Color standardColor = GUI.color;
			if (obj != null)
			{
				GUI.color = Color.green;
			}
			else
			{
				GUI.color = Color.red;
			}
			
			newObj = (GameObject)EditorGUILayout.ObjectField(obj, typeof(GameObject), true);
			EditorGUILayout.EndVertical();



			GUI.color = standardColor;

			EditorGUILayout.EndHorizontal();

			try
			{
				if (obj != newObj)
				{
					UpdatePath(path, ChildPath(newObj));
				}

				if (newPath != path)
				{
					UpdatePath(path, newPath);
				}
			}
			catch (UnityException ex)
			{
				Debug.LogError(ex.Message);
			}
		}

		#region CLIP PARAMETER HELPERS
		// Adapted from Sebastian Krośkiewicz's Unity Animation Hierarchy Editor
		// https://github.com/s-m-k/Unity-Animation-Hierarchy-Editor

		private ArrayList pathsKeys;
		private Hashtable paths;

		private string _originalPropertyName = "Property Name";
		private string _newPropertyName = "New Parent Name / New Property Name";

		private string _replacementOldRoot;
		private string _replacementNewRoot;

		private Dictionary<string, string> _tempPathOverrides;

		private void GetAnimationClipPathsAndKeys()
		{
			paths = new Hashtable();
			pathsKeys = new ArrayList();

			GetPathAndKey(AnimationUtility.GetCurveBindings(animClip));
			GetPathAndKey(AnimationUtility.GetObjectReferenceCurveBindings(animClip));
		}

		private void GetPathAndKey(EditorCurveBinding[] curves)
		{
			foreach (EditorCurveBinding curveData in curves)
			{
				string key = curveData.path;

				if (paths.ContainsKey(key))
				{
					((ArrayList)paths[key]).Add(curveData);
				}
				else
				{
					ArrayList newProperties = new ArrayList();
					newProperties.Add(curveData);
					paths.Add(key, newProperties);
					pathsKeys.Add(key);
				}
			}
		}

		private void ReplaceRoot(string oldRoot, string newRoot)
		{
			float progressPercentage = 0.0f;

			_replacementOldRoot = oldRoot;
			_replacementNewRoot = newRoot;

			AssetDatabase.StartAssetEditing();
			
			Undo.RegisterCompleteObjectUndo(animClip, "Rename Clip Property Change");

			for (int iCurrentPath = 0; iCurrentPath < pathsKeys.Count; iCurrentPath++)
			{
				string path = pathsKeys[iCurrentPath] as string;
				ArrayList curves = (ArrayList)paths[path];

				for (int i = 0; i < curves.Count; i++)
				{
					EditorCurveBinding binding = (EditorCurveBinding)curves[i];

					if (path.Contains(_replacementOldRoot))
					{
						if (!path.Contains(_replacementNewRoot))
						{
							string sNewPath = Regex.Replace(path, "^" + _replacementOldRoot, _replacementNewRoot);

							AnimationCurve curve = AnimationUtility.GetEditorCurve(animClip, binding);
							if (curve != null)
							{
								AnimationUtility.SetEditorCurve(animClip, binding, null);
								binding.path = sNewPath;
								AnimationUtility.SetEditorCurve(animClip, binding, curve);
							}
							else
							{
								ObjectReferenceKeyframe[] objectReferenceCurve = AnimationUtility.GetObjectReferenceCurve(animClip, binding);
								AnimationUtility.SetObjectReferenceCurve(animClip, binding, null);
								binding.path = sNewPath;
								AnimationUtility.SetObjectReferenceCurve(animClip, binding, objectReferenceCurve);
							}
						}
					}
				}

				// Update the progress meter
				progressPercentage = (float)iCurrentPath / (float)pathsKeys.Count;

				EditorUtility.DisplayProgressBar(
				"Renaming Clip Property Progress",
				"How far along the animation editing has progressed.",
				progressPercentage);
			}

			AssetDatabase.StopAssetEditing();
			EditorUtility.ClearProgressBar();

			GetAnimationClipPathsAndKeys();
			AssetDatabase.SaveAssets();
		}

		private void UpdatePath(string oldPath, string newPath)
		{
			if (paths[newPath] != null)
			{
				throw new UnityException("Path " + newPath + " already exists in that animation!");
			}

			AssetDatabase.StartAssetEditing();
			
			Undo.RegisterCompleteObjectUndo(animClip, "Rename Clip Property Change");

			// recreating all curves one by one to maintain proper order in the editor - 
			// slower than just removing old curve and adding a corrected one, but it's more user-friendly
			for (int iCurrentPath = 0; iCurrentPath < pathsKeys.Count; iCurrentPath++)
			{
				string path = pathsKeys[iCurrentPath] as string;
				ArrayList curves = (ArrayList)paths[path];

				for (int i = 0; i < curves.Count; i++)
				{
					EditorCurveBinding binding = (EditorCurveBinding)curves[i];
					AnimationCurve curve = AnimationUtility.GetEditorCurve(animClip, binding);
					ObjectReferenceKeyframe[] objectReferenceCurve = AnimationUtility.GetObjectReferenceCurve(animClip, binding);


					if (curve != null)
					{
						AnimationUtility.SetEditorCurve(animClip, binding, null);
					}
					else
					{
						AnimationUtility.SetObjectReferenceCurve(animClip, binding, null);
					}

					if (path == oldPath)
					{
						binding.path = newPath;
					}

					if (curve != null)
					{
						AnimationUtility.SetEditorCurve(animClip, binding, curve);
					}
					else
					{
						AnimationUtility.SetObjectReferenceCurve(animClip, binding, objectReferenceCurve);
					}

					
					float fProgress = (float)iCurrentPath / (float)pathsKeys.Count;

					EditorUtility.DisplayProgressBar(
					"Renaming Clip Property Progress",
					"How far along the animation editing has progressed.",
					fProgress);
				}
			}

			AssetDatabase.StopAssetEditing();

			EditorUtility.ClearProgressBar();

			GetAnimationClipPathsAndKeys();
			AssetDatabase.SaveAssets();
		}

		private GameObject FindObjectInRoot(string path)
		{
			if (animatorReference == null) { return null; }

			Transform child = animatorReference.transform.Find(path);

			if (child != null)
			{
				return child.gameObject;
			}
			else
			{
				return null;
			}
		}

		private string ChildPath(GameObject obj, bool sep = false)
		{
			if (animatorReference == null)
			{
				throw new UnityException("Please assign Referenced Animator (Root) first!");
			}

			if (obj == animatorReference.gameObject)
			{
				return "";
			}
			else
			{
				if (obj.transform.parent == null)
				{
					throw new UnityException("Object must belong to " + animatorReference.ToString() + "!");
				}
				else
				{
					return ChildPath(obj.transform.parent.gameObject, true) + obj.name + (sep ? "/" : "");
				}
			}
		}
		#endregion

		#endregion
	}
}