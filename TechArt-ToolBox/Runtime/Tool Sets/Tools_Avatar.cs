using System.Collections.Generic;
using System.Linq;

using UnityEngine;
using UnityEngine.AI;

using VRR.Avatar.Hands;
using VRR.Avatars.EyeGaze;
using VRR.Avatars.Navigation;

using VRR.Input;
using VRR.Interactions;
using VRR.Libraries;


#if USE_MESH_BAKER
using VRR.Integrations.MeshBaker;
#endif

#if USE_FINAL_IK
using VRR.Integrations.FinalIK;
#endif

#if USE_REALISTIC_EYES
using VRR.Integrations.RealisticEyeMovements;
#endif


namespace VRR.TechArtTools.Avatar
{
    public class Tools_Avatar : ScriptableObject
    {
        public GameObject skeleton;

        [SerializeField] private GameObject[] _meshObjects;
        // NOT USED HERE
        bool UseNewBindingPose = false;

        public GameObject[] GetMeshes()
        {
            return _meshObjects;
        }


        #region RESKIN MESH
        public void ReSkinMesh(SkinnedMeshRenderer renderer, GameObject goalSkeleton, bool simpleCopy)
        {
            /// Skeletons (ie Bones) have 3 components that need to be managed:
            /// - Bones Transform Array (the bones themselves as referenced by the skinnedMeshRenderer.bones)
            /// - Bone Bind Pose Array (as referenced by skinnedMeshRenderer.sharedMesh.bindPoses )
            /// - Bone Weights Array (as referenced by skinnedMeshRenderer.sharedMesh.boneWeights )
            /// 
            /// Bind Poses are arranged in the same order as the Bones Transform Array for a 1:1 reference
            /// and each Bone Weight references the bone index value from the Bones Transform Array

            Transform[] originalBoneArray = renderer.bones;
            BoneWeight[] originalWeights = renderer.sharedMesh.boneWeights;
            Matrix4x4[] originalBindPoses = renderer.sharedMesh.bindposes;

            // get all the active gameobjects that are children of our target skeleton
            Transform[] newBoneArray = GetActiveChildren(goalSkeleton);

            // apply the new bone array to our renderer
            renderer.bones = newBoneArray;

            Debug.Log("Number of Original Bones: " + originalBoneArray.Length + "\n"
                + "Number of Original Weights: " + originalWeights.Length + "\n"
                + "Number of Original BindPoses: " + originalBindPoses.Length + "\n"
                + "Number of New Bones: " + newBoneArray.Length);

            if (!simpleCopy)
            {
                renderer.sharedMesh.bindposes = GetNewBindPoseArray(originalBindPoses, originalBoneArray, newBoneArray);
                renderer.sharedMesh.boneWeights = GetNewBoneWeights(originalWeights, originalBoneArray, newBoneArray);
            }
        }

        Dictionary<string, int> _confirmedBones;
        Dictionary<string, int> _replacementBones;
        private Transform[] GetActiveChildren(GameObject parent)
        {
            List<Transform> activeChildren = new List<Transform>();

            Transform[] allChildren = parent.GetComponentsInChildren<Transform>();

            // loop through all the child objects, only get those that are active
            for (int i = 0; i < allChildren.Length; i++)
            {
                if (allChildren[i].gameObject.activeInHierarchy)
                {
                    activeChildren.Add(allChildren[i]);
                }
            }

            return activeChildren.ToArray();
        }

        /// <summary>
        /// We create a new bind pose array
        /// </summary>
        public Matrix4x4[] GetNewBindPoseArray(Matrix4x4[] originalBindPoses, Transform[] originalBoneArray, Transform[] newBoneArray)
        {
            // Create a matching BindPose array - length and index should match new Bone Array            
            Matrix4x4[] newBindPoses = new Matrix4x4[newBoneArray.Length];

            // NOTE:
            // BindPose is the starting position/orientation of a bone (ie bone's inverse transformation matrix)
            // when the mesh was skinned relative to its root transform


            for (int i = 0; i < newBindPoses.Length; i++)
            {
                // set current bindPose value to default ZERO
                newBindPoses[i] = Matrix4x4.zero;

                // get matching bone name from the new bone array...
                string currentBoneName = newBoneArray[i].name;

                // loop through original bone array, looking for matching names...
                for (int j = 0; j < originalBoneArray.Length; j++)
                {
                    // if we match the name with a name in the old bone array...
                    if (currentBoneName == originalBoneArray[j].name)
                    {
                        // use its index to retrieve the bindPose value

                        if (UseNewBindingPose)
                        {
                            // The bind pose is bone's inverse transformation matrix
                            // In this case we also make this matrix relative to the root
                            // So that we can move the root game object around freely
                            // example:
                            // bindPoses[i] = bones[i].worldToLocalMatrix * root.transform.localToWorldMatrix;
                            // the root will commonly either be at origin (0,0,0) or the hips

                            newBindPoses[i] = newBoneArray[i].worldToLocalMatrix * newBoneArray[0].transform.localToWorldMatrix;
                            //Debug.Log(i + ") " + currentBoneName + " Bind Pose = " + i + ") " + newBoneArray[i].name);
                        }
                        else
                        {
                            newBindPoses[i] = originalBindPoses[j];
                            //Debug.Log(i + ") " + currentBoneName + " Bind Pose = " + j + ") " + originalBoneArray[j].name);
                        }

                        break;
                    }
                }
            }

            return newBindPoses;
        }

        /// <summary>
        /// We convert the bone index (of the boneWeight data struct) that referenced the orginal bone array to the new bone array
        /// </summary>
        public BoneWeight[] GetNewBoneWeights(BoneWeight[] originalBoneWeights, Transform[] originalBoneArray, Transform[] newBoneArray)
        {
            _confirmedBones = new Dictionary<string, int>();
            _replacementBones = new Dictionary<string, int>();

            /// Bone Weights are per vertex, so the array is aligned with the vertice's numeration 
            /// (eg vertex 0 = boneWeights[0]... vertex 100 = boneWeights[100])
            /// 
            /// Copy over weights, so we maintain the weight values... 
            BoneWeight[] newBoneWeights = originalBoneWeights;

            /// UPDATE BONE WEIGHT INDICES TO REFERENCE NEW ARRAY ORDER
            /// loop through all the weights:
            /// - get index value from original weight
            /// - use index to find bone name from original bone array
            /// - use name to find match in new bone array
            /// - set index value in our new weights to those found in the new array

            // Debug.Log("Number of Vertices: " + newBoneWeights.Length + " : " + originalBoneWeights.Length + " | New Bone Array: " + newBoneArray.Length);

            for (int i = 0; i < newBoneWeights.Length; i++)
            {
                // cache bone indices...
                int boneIndex0 = originalBoneWeights[i].boneIndex0;
                int boneIndex1 = originalBoneWeights[i].boneIndex1;
                int boneIndex2 = originalBoneWeights[i].boneIndex2;
                int boneIndex3 = originalBoneWeights[i].boneIndex3;

                //Debug.Log("<color=cyan>Vertex [" + i + "] </color>" +
                //                    originalBoneArray[boneIndex0].name + " , " +
                //                    originalBoneArray[boneIndex1].name + " , " +
                //                    originalBoneArray[boneIndex2].name + " , " +
                //                    originalBoneArray[boneIndex3].name + " | " +
                //                    originalBoneArray.Length);

                newBoneWeights[i].boneIndex0 = GetNewBoneIndex(originalBoneArray[boneIndex0], newBoneArray);
                newBoneWeights[i].boneIndex1 = GetNewBoneIndex(originalBoneArray[boneIndex1], newBoneArray);
                newBoneWeights[i].boneIndex2 = GetNewBoneIndex(originalBoneArray[boneIndex2], newBoneArray);
                newBoneWeights[i].boneIndex3 = GetNewBoneIndex(originalBoneArray[boneIndex3], newBoneArray);
            }

            return newBoneWeights;
        }

        public int GetNewBoneIndex(Transform originalBone, Transform[] newBones)
        {
            // *sometimes combined meshes will have multiple references to a bone, 
            // *because, prior to being combined, each piece was rigged to a similar base skeleton

            // check if we've already confirmed the existence of the bone and return its index
            if (_confirmedBones.ContainsKey(originalBone.name))
            {
                return _confirmedBones[originalBone.name];
            }
            else // check our list of replacement bones in case this bone is known to not exist in the new base rig
            if (_replacementBones.ContainsKey(originalBone.name))
            {
                return _replacementBones[originalBone.name];
            }


            // loop through array and compare bone names, then capture the index value
            for (int i = 0; i < newBones.Length; i++)
            {
                if (originalBone.name == newBones[i].name)
                {
                    // Debug.Log(originalBone.name + " found at index [" + i + "]");

                    _confirmedBones.Add(originalBone.name, i);

                    return _confirmedBones[originalBone.name];
                }
            }

            // if we go through the loop and the bone does not exist in our new base skeleton, 
            // find its nearest parent as a replacement...
            return FindReplacementBone(originalBone, newBones);
        }

        private int FindReplacementBone(Transform boneToReplace, Transform[] newBoneArray)
        {
            Debug.LogWarning(">>>>> Finding parent replacement for [" + boneToReplace.name + "]...");

            Transform bone = boneToReplace;
            int replacementIndex = 0;

            while (bone.parent != null)
            {
                for (int i = 0; i < newBoneArray.Length; i++)
                {
                    if (bone.parent.name == newBoneArray[i].name)
                    {
                        Debug.Log("<<<<< Replaced with [" + newBoneArray[i].name + "]");

                        replacementIndex = i;
                        break;
                    }
                }

                // go to next parent
                bone = bone.parent.transform;
            }

            //add the old bone to the list and its replacement index...
            _replacementBones.Add(boneToReplace.name, replacementIndex);

            if (replacementIndex == 0)
            {
                Debug.LogWarning("<<<<< No Replacement Found for " + bone.name + " - Returning 0");
            }

            return _replacementBones[boneToReplace.name];
        }

        #endregion


        #region AVATAR PREFAB SETUP

        public GameObject CreateEyeGazeController(GameObject mesh)
        {
            // Create GAME OBJECT
            GameObject controller = new GameObject("Eye Gaze");
            #if USE_REALISTIC_EYES
            RealisticEyeMovementIntegration.AddGazeControllerComponents(controller, mesh);
            #endif
            return controller;
        }

        #region COMPONENT SETUPS

        public void SetupSkeletonComponents(GameObject SKELETON)
        { 
            #if UNITY_EDITOR && USE_FINAL_IK
            FinalIKIntegration.SetupSkeletonComponents(SKELETON);
            #endif
        }

        public void SetupMeshComponents(GameObject bakedMesh)
        {
            // MESH COMPONENTS
            #if UNITY_EDITOR && USE_MESH_BAKER
            MeshBakerIntegration.RemovedBakeComponent(bakedMesh);
            #endif

            // - EyeGaze
            #if UNITY_EDITOR && USE_REALISTIC_EYES
            RealisticEyeMovementIntegration.SetupEyegaze(bakedMesh, skeleton);
            #endif

            // - LookAtIK
            #if UNITY_EDITOR && USE_FINAL_IK
            FinalIKIntegration.AddLookAtComponent(bakedMesh, skeleton);
            #endif
        }

        public void SetupPrefabComponents(GameObject PREFAB, GameObject selectedActor)
        {
            // Instantiate the selected ACTOR Prefab
            GameObject ACTOR = InstantiateActor(selectedActor, PREFAB.transform);
            // Get REFS to our commonly used GameObjects
            GameObject MESH = ACTOR.transform.GetChild(0).gameObject;
            GameObject SKELETON = ACTOR.transform.GetChild(1).gameObject;


            SetupAnimator(PREFAB);
            SetupRigidBody(PREFAB);
            //SetupObjectOfInterest(PREFAB);

            //SpeechInjector speech = prefab.AddComponent<SpeechInjector>();
            VRR_Character_Navigation navigation = PREFAB.AddComponent<VRR_Character_Navigation>();
            navigation.enabled = false;

            

            // Create our CONTROLLER PARENT OBJECT
            GameObject CONTROLLERS = new GameObject("CONTROLLERS");
            CONTROLLERS.transform.SetParent(PREFAB.transform);

            // Navigation
            GameObject navControlObject = CreateNavigationManager();
            navControlObject.transform.SetParent(CONTROLLERS.transform);
                    
            // EyeGaze
            GameObject eyegazeControlObject = CreateEyeGazeController(MESH);
            eyegazeControlObject.transform.SetParent(CONTROLLERS.transform);

            #if USE_FINAL_IK
            // FinalIK Biped Skeleton
            GameObject ikControlObject = CreateIKController();
            ikControlObject.transform.SetParent(CONTROLLERS.transform);
            FinalIKIntegration.SetupIKController(ikControlObject, SKELETON);

            // FinalIK Interaction System
            GameObject interactionControlObject = CreateInteractionManager();
            interactionControlObject.transform.SetParent(CONTROLLERS.transform);
            FinalIKIntegration.SetupInteractionManager(interactionControlObject, MESH, SKELETON);
            #endif
                    

            // PASS CLIP TO SPEECH
            //if (_speakClip != null)
            //{
            //    //speech.GetComponent<SpeechInjector>().SetClipToReplace(_speakClip);
            //}

        }

        private void SetupAnimator(GameObject prefab)
        {
            prefab.AddComponent<Animator>();
        }
        private void SetupRigidBody(GameObject prefab)
        {
            Rigidbody rigidBody = prefab.AddComponent<Rigidbody>();
            rigidBody.isKinematic = true;

        }
        private void SetupObjectOfInterest(GameObject prefab)
        {
            VRR_ObjectOfInterest ooi = prefab.AddComponent<VRR_ObjectOfInterest>();

            if(skeleton == null)
            {
                skeleton = prefab.transform.GetChild(1).gameObject;
            }

            // CONNECT POINTS OF INTEREST - LOOK INTO MY EYES!
            if (skeleton != null)
            {
                Transform leftEyeBone  = FindGameObjectInSkeleton("Eyeball_L").transform;
                Transform rightEyeBone = FindGameObjectInSkeleton("Eyeball_R").transform;

                if (leftEyeBone != null && rightEyeBone != null)
                {
                    ooi.SetPointsOfInterest(new Transform[] { leftEyeBone, rightEyeBone });
                }
            }
            else
            {
                Debug.Log("<color=red>No Skeleton Reference!</color>");
            }
            ooi.SetWeight(ObjectInterestWeight.Low);
        }
        #endregion

        public GameObject[] InstantiateActorBakingObjects(GameObject parent, GameObject[] meshes)
        {
            GameObject[] instantiatedObjects = new GameObject[meshes.Length];

            for (int i = 0; i < instantiatedObjects.Length; i++)
            {
                instantiatedObjects[i] = GameObject.Instantiate(meshes[i]);
                instantiatedObjects[i].transform.SetParent(parent.transform);
            }

            int numberOfBonesFound = 0;

            // Get a REFERENCE to the Skeleton from the mesh with the most bones
            foreach (var obj in instantiatedObjects)
            {
                GameObject root = null;// = obj.transform.Find("SKELETON").gameObject;

                Transform[] allTheChildren = obj.GetComponentsInChildren<Transform>();

                foreach(var currentChild in allTheChildren)
                {
                    if(currentChild.name == "SKELETON")
                    {
                        root = currentChild.gameObject;
                        break;
                    }
                }

                if(root == null) { return null; }

                if (root.transform.childCount > numberOfBonesFound)
                {
                    // cache the most bones
                    numberOfBonesFound = root.transform.childCount;

                    // cache the skeleton reference
                    skeleton = root;
                }
            }

            // now pass to our MeshBaker Combiner Object
            return instantiatedObjects;
        }
        public GameObject InstantiateActor(GameObject actor, Transform parent)
        {
            GameObject ACTOR = Instantiate(actor, parent.transform);
            ACTOR.name = "ACTOR";

            return ACTOR;
        }
#endregion



#region VR HANDS

#region EDITOR SET PROPERTIES
#pragma warning disable 0649
        [SerializeField] private GameObject             _hand;
        [SerializeField] private ControllerHand         _handedness;
        [SerializeField] private string[]               _fingerNames;
        [SerializeField] private string[]               _thumbNames;
        [SerializeField] private Library_HandPoses      _library;
        [SerializeField] private SkinnedMeshRenderer    _handMesh;
#pragma warning restore 0649
#endregion

        string _controllerObject_Name = "CONTROLLERS";
        string _rotationGoal_name     = "ROTATION GOALS";

        GameObject _controllerObject;
        GameObject _handControllerObject;

        Transform[] _children;

        HandInput_Manager   _inputManager;
        //HandTracking        _trackingManager;
        HandInteraction     _interactionManager;
        HandPose_Manager    _poseManager;
        HandPose_Controller _poseController;

        Dictionary<string, List<Transform>> _fingerJoints;
        Dictionary<string, List<Transform>> _thumbJoints;

        Phalange_Finger[] _fingers;
        Phalange_Thumb[] _thumbs;

#region GETTERS
        public string GetName()
        {
            if (_hand != null)
            {
                return _hand.name;
            }

            return "null";
        }
#endregion

#region SETTERS
        public void SetHandedness(ControllerHand handedness)
        {
            _handedness = handedness;
        }

        public void SetHandObject(GameObject hand)
        {
            _hand = hand;
        }

        public void SetHandMesh(SkinnedMeshRenderer mesh)
        {
            _handMesh = mesh;
        }

        public void SetFingerNamesArray(string[] names)
        {
            _fingerNames = names;
        }

        public void SetThumbNamesArray(string[] names)
        {
            _thumbNames = names;
        }

        public void SetLibrary(Library_HandPoses library)
        {
            _library = library;
        }

        private void SetDefaultNames()
        {
            if (_fingerNames == null || _fingerNames.Length == 0)
            {
                _fingerNames = new string[4];
                _fingerNames[0] = "index";
                _fingerNames[1] = "middle";
                _fingerNames[2] = "ring";
                _fingerNames[3] = "pinky";
            }

            if (_thumbNames == null || _thumbNames.Length == 0)
            {
                _thumbNames = new string[1];
                _thumbNames[0] = "thumb";
            }
        }

#endregion

#region SETUP VR HANDS
        public void Setup()
        {
            if (_hand == null) 
            {
                Debug.Log("_hand not set! ");
                return; 
            }

            SetDefaultNames();

            Setup_Controllers();
           
            _children     = _hand.GetComponentsInChildren<Transform>();

            _fingerJoints = new Dictionary<string, List<Transform>>();
            _fingerJoints = Setup_Dictionary(_fingerNames, _children);

            _thumbJoints  = new Dictionary<string, List<Transform>>();
            _thumbJoints  = Setup_Dictionary(_thumbNames,  _children);

            _poseController.Hand = new Phalanges_Hand();
            _poseController.Hand.SetFingerArray(Setup_Fingers(_fingerJoints));
            _poseController.Hand.SetThumbArray(Setup_Thumbs(_thumbJoints));
        }
        
#region SETUP VR HAND CONTROLLERS
        private void Setup_Controllers()
        {
            Setup_MainController();
            Setup_HandController();
            Setup_Managers(_handedness);
        }

        private void Setup_MainController()
        {
            _controllerObject = GameObject.Find(_controllerObject_Name);

            if (_controllerObject == null)
            {
                // if non existent, make a new one
                _controllerObject = new GameObject(_controllerObject_Name);
                _controllerObject.transform.SetParent(_hand.transform.parent);
                _controllerObject.transform.localPosition = Vector3.zero;
                _controllerObject.transform.localRotation = Quaternion.identity;


                _inputManager = _controllerObject.AddComponent<HandInput_Manager>();
                //_trackingManager = _controllerObject.AddComponent<HandTracking>();
            }
            else
            {
                // else, get the related components
                _inputManager = _controllerObject.GetComponent<HandInput_Manager>();
                //_trackingManager = _controllerObject.GetComponent<HandTracking>();
            }

        }

        private void Setup_HandController()
        {
            _handControllerObject = GameObject.Find(_handedness.ToString());

            if (_handControllerObject == null)
            {
                // if non existent, make a new one
                _handControllerObject = new GameObject(_handedness.ToString());
                _handControllerObject.transform.SetParent(_controllerObject.transform);
                _handControllerObject.transform.localPosition = Vector3.zero;
                _handControllerObject.transform.localRotation = Quaternion.identity;

                _poseManager = _handControllerObject.AddComponent<HandPose_Manager>();
                _poseController = _handControllerObject.AddComponent<HandPose_Controller>();
            }
            else
            {
                // else, get the related components
                _poseManager = _handControllerObject.GetComponent<HandPose_Manager>();
                _poseController = _handControllerObject.GetComponent<HandPose_Controller>();
            }
        }

        private void Setup_Managers(ControllerHand handedness)
        {
            _interactionManager = _hand.GetComponent<HandInteraction>();

            if (_interactionManager == null)
            {
                _interactionManager = _hand.AddComponent<HandInteraction>();
            }

            _interactionManager.SetHandedness(handedness);
            
            _inputManager.SetPoseManager(handedness, _poseManager);
            
            _poseManager.SetHandPoseController(_poseController);
            _poseManager.SetHandInteraction(_interactionManager);
            _poseManager.SetLibrary(_library);

            _poseController.SetSkinnedMesh(_handMesh);

            //_trackingManager.SetHandBone(_hand.transform, handedness);
        }
#endregion

        private Dictionary<string, List<Transform>> Setup_Dictionary(string[] phalangeNames, Transform[] children)
        {
            // Step 1: create our dictionary that will hold the list of names and the associated joint arrays
            Dictionary<string, List<Transform>> dict = new Dictionary<string, List<Transform>>();

            // Step 2: loop through each phalangeName
            foreach (string currentName in phalangeNames)
            {
                // Step 3: create a list to cache any joints we find
                List<Transform> joints = new List<Transform>();

                // Step 4: for each name, loop through all the child objects to find a name match
                for (int i = 0; i < children.Length; i++)
                {
                    // Step 5: set names to all lower case for easier matching
                    string currentPhalangeName = currentName.ToLower();
                    string currentChildName = children[i].name.ToLower();

                    // Step 6: if the child name matches the current phalangeName
                    if (currentChildName.Contains(currentPhalangeName))
                    {
                        // Step 6b: then add it to our list
                        joints.Add(children[i]);
                    }
                }

                // Step 7: once the list is complete, add it to the dictionary
                dict.Add(currentName.ToUpper(), joints);
            }

            // Step 8: return our completed dictionary
            return dict;
        }

        private Phalange_Finger[] Setup_Fingers(Dictionary<string, List<Transform>> dict)
        {
            // DEFAULT Rotation Values
            MinMax minMax;
            minMax.In = 90;
            minMax.Out = 10;

            // Step 1: create new array of fingers that is the length of our dictionary
            Phalange_Finger[] phalanges = new Phalange_Finger[dict.Count];

            // Step 2: Loop through our dictionary assigning the retlated Keys and Values to each finger
            for (int i = 0; i < phalanges.Length; i++)
            {
                // Step 3: get a reference to our current KEY VALUE PAIR
                string KEY_fingerName = dict.Keys.ElementAt(i);
                Transform[] VALUE_jointArray = dict.Values.ElementAt(i).ToArray();

                // Step 4: create finger
                phalanges[i] = new Phalange_Finger();

                // Step 4a: [Key] is the name of the finger
                phalanges[i].SetName(KEY_fingerName);

                // Step 4b: [Value] is the arry of joints
                phalanges[i].SetJoints(VALUE_jointArray);

                // Step 4c: set our default Min Max Rotation values
                phalanges[i].SetMinMaxRotation(minMax);

                // Step 5: get a reference to our finger's root joint
                Transform phalangeBaseJoint   = phalanges[i].GetPhalangeRoot();

                // Step 6: Setup our rotation goals, then add to our finger's properties
                phalanges[i].SetRotationGoals(Setup_RotationGoal(KEY_fingerName, phalangeBaseJoint));
            }

            // return the completed finger setup
            return phalanges;
        }

        private Phalange_Thumb[] Setup_Thumbs(Dictionary<string, List<Transform>> dict)
        {
            MinMax minMax;
            minMax.In = 60;
            minMax.Out = 40;

            // Step 1: create new array of fingers that is the length of our dictionary
            Phalange_Thumb[] phalanges = new Phalange_Thumb[dict.Count];

            // Step 2: Loop through our dictionary assigning the retlated Keys and Values to each finger
            for (int i = 0; i < phalanges.Length; i++)
            {
                string KEY_fingerName = dict.Keys.ElementAt(i);
                Transform[] VALUE_jointArray = dict.Values.ElementAt(i).ToArray();

                phalanges[i] = new Phalange_Thumb();

                // Step 3a: [Key] is the name of the finger
                phalanges[i].SetName(KEY_fingerName);

                // Step 3b: [Value] is the arry of joints
                phalanges[i].SetJoints(VALUE_jointArray);

                phalanges[i].SetMinMaxRotation(minMax);

                // Step 5: get a reference to our finger's root joint
                Transform phalangeBaseJoint = phalanges[i].GetPhalangeRoot();

                phalanges[i].SetRotationGoals(Setup_RotationGoal(KEY_fingerName, phalangeBaseJoint));
            }

            return phalanges;
        }

        private RotationGoals Setup_RotationGoal(string goalName, Transform baseJoint)
        {
            GameObject rotationGoal;

            // Step 1: see if the transform exists as a child of our hand (ie finger's parent)
            if (baseJoint.parent.Find(_rotationGoal_name))
            {
                // Step 1a: if it exists, cache the gameObject
                rotationGoal = baseJoint.parent.Find(_rotationGoal_name).gameObject;
            }
            else
            {
                // Step 1b: if not, create a new gameObject and zero out it's transforms
                rotationGoal = new GameObject(_rotationGoal_name);
                SetTransformValues(rotationGoal, baseJoint.parent, Vector3.zero, Quaternion.Euler(Vector3.zero));
            }

            // Step 2: create new IN & OUT goals
            GameObject fingerGoal_IN  = new GameObject(goalName + "_IN");
            GameObject fingerGoal_OUT = new GameObject(goalName + "_OUT");

            // Step 3: parent goals to the rotationGoal parent object and match the Transform to finger's base joint
            SetTransformValues(fingerGoal_IN, rotationGoal.transform, baseJoint.localPosition, baseJoint.localRotation);
            SetTransformValues(fingerGoal_OUT, rotationGoal.transform, baseJoint.localPosition, baseJoint.localRotation);

            // Step 4: create goal reference and assign the values
            RotationGoals fingerGoals;
            fingerGoals.In  = fingerGoal_IN.transform;
            fingerGoals.Out = fingerGoal_OUT.transform;

            // step 5: pass the final RotationGoals
            return fingerGoals;
        }

        private void SetTransformValues(GameObject target, Transform parent, Vector3 position, Quaternion rotation)
        {
            target.transform.SetParent(parent);
            target.transform.localPosition = position;
            target.transform.localRotation = rotation;
            target.transform.localScale = Vector3.one;
        }
#endregion

#endregion


        public GameObject FindGameObjectInSkeleton(string targetBoneName)
        {
            Transform[] bones = skeleton.GetComponentsInChildren<Transform>();
            GameObject targetBone = null;

            foreach (var bone in bones)
            {
                if (bone.name == targetBoneName)
                {
                    targetBone = bone.gameObject;
                }
            }

            return targetBone;
        }



        #region CREATE
        private GameObject CreateIKController()
        {
            // Create GAMEOBJECT
            GameObject controller = new GameObject("IK Controller");

            // Create Effector OBJECTS & Parent
            GameObject leftHand = CreateEffectorObjects("Left Hand");
            leftHand.transform.SetParent(controller.transform);

            GameObject rightHand = CreateEffectorObjects("Right Hand");
            rightHand.transform.SetParent(controller.transform);

            GameObject leftFoot = CreateEffectorObjects("Left Foot");
            leftFoot.transform.SetParent(controller.transform);

            GameObject rightFoot = CreateEffectorObjects("Right Foot");
            rightFoot.transform.SetParent(controller.transform);

            return controller;
        }

        private GameObject CreateEffectorObjects(string name)
        {
            GameObject effector = new GameObject(name);
            GameObject target = new GameObject(name + " - TARGET");
            GameObject bend = new GameObject(name + " - BEND");

            target.transform.SetParent(effector.transform);
            bend.transform.SetParent(effector.transform);

            return effector;
        }

        private GameObject CreateNavigationManager()
        {
            // Create GAMEOBJECT
            GameObject controller = new GameObject("Navigation Manager");

            // ADD COMPONENTS
            NavMeshAgent agent = controller.AddComponent<NavMeshAgent>();
            agent.enabled = false;

            NavMeshObstacle obstacle = controller.AddComponent<NavMeshObstacle>();
            obstacle.shape = NavMeshObstacleShape.Capsule;
            obstacle.center = new Vector3(0, 1, 0);
            obstacle.radius = 0.3f;
            obstacle.height = 2.0f;
            obstacle.carving = true;
            obstacle.carvingMoveThreshold = 0.1f;
            obstacle.carvingTimeToStationary = 0.5f;
            obstacle.carveOnlyStationary = true;
            obstacle.enabled = false;

            // Enable our Nav Agent
            // NOTE - only one (Agent or Obstacle) can be enabled at a time
            agent.enabled = true;

            return controller;
        }

        private GameObject CreateInteractionManager()
        {
            GameObject controller = new GameObject("Interaction System");
            return controller;
        }
        #endregion
    }
}