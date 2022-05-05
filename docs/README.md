# Docs

The latest Doxygen documentation can be viewed here:

https://golioth-zephyr-sdk-doxygen.firebaseapp.com/


### Building Doxygen Locally

CI will automatically run Doxygen and deploy to Firebase.
You can also build locally to verify there are no warnings or errors in doxygen before pushing.

Install dependencies:

```
sudo apt install doxygen graphviz
```

Build:

```sh
cd doxygen
doxygen
```

Output will be placed in `generated_docs/`.

### Deploying to Firebase Locally

Deployment of Doxygen-generated files is done automatically in CI, so you shouldn't need
to manually deploy.

You should only need to do this if you are changing something in Firebase and you want to test.

Install Firebase following the official [Firebase docs](https://firebase.google.com/docs/cli).

Serve locally, for testing:

```sh
cd firebase
firebase serve --only hosting
```

Deploy to Firebase `golioth-zephyr-sdk-doxygen-dev`:

```sh
cd firebase
firebase login
firebase deploy --only docs-dev
```
