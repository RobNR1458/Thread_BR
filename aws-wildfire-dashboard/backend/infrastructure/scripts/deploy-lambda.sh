#!/bin/bash
set -e

echo "⚡ Deploying Lambda Functions for Wildfire Detection System..."

REGION="us-east-2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAMBDA_DIR="${SCRIPT_DIR}/../../lambda"

# Get Lambda role ARN
LAMBDA_ROLE_ARN=$(aws cloudformation list-exports --region $REGION --query "Exports[?Name=='WildfireLambdaRoleArn'].Value" --output text)

if [ -z "$LAMBDA_ROLE_ARN" ]; then
    echo "❌ Error: Could not find WildfireLambdaRoleArn in CloudFormation exports"
    echo "   Make sure you've deployed the IAM roles stack first:"
    echo "   aws cloudformation deploy --template-file cloudformation/04-iam-roles.yaml --stack-name wildfire-sensor-iam-roles --capabilities CAPABILITY_NAMED_IAM"
    exit 1
fi

echo "Lambda Role ARN: $LAMBDA_ROLE_ARN"
echo ""

# Array of Lambda functions to deploy
LAMBDA_FUNCTIONS=(
    "data-enricher"
    "ml-processor"
    "api-handlers/realtime:api-realtime"
    "api-handlers/historical:api-historical"
    "api-handlers/alerts:api-alerts"
)

for FUNCTION_PATH in "${LAMBDA_FUNCTIONS[@]}"; do
    # Parse function path and name
    if [[ $FUNCTION_PATH == *":"* ]]; then
        # Has custom function name
        FUNC_DIR=$(echo $FUNCTION_PATH | cut -d':' -f1)
        FUNC_NAME=$(echo $FUNCTION_PATH | cut -d':' -f2)
    else
        # Use directory name as function name
        FUNC_DIR=$FUNCTION_PATH
        FUNC_NAME=$(basename $FUNCTION_PATH)
    fi

    echo "=================================================="
    echo "📦 Processing: $FUNC_NAME (from $FUNC_DIR)"
    echo "=================================================="

    cd "${LAMBDA_DIR}/${FUNC_DIR}"

    # Install dependencies
    if [ -f "package.json" ]; then
        echo "📥 Installing dependencies..."
        npm install --production --silent
    fi

    # Create deployment package
    echo "📦 Creating deployment package..."
    zip -rq /tmp/${FUNC_NAME}.zip . -x "*.git*" "node_modules/aws-sdk/*" "package-lock.json"

    # Check if function exists
    if aws lambda get-function --function-name $FUNC_NAME --region $REGION &>/dev/null; then
        echo "🔄 Updating existing function..."
        aws lambda update-function-code \
            --function-name $FUNC_NAME \
            --zip-file fileb:///tmp/${FUNC_NAME}.zip \
            --region $REGION \
            --no-cli-pager > /dev/null

        # Update configuration if needed
        aws lambda update-function-configuration \
            --function-name $FUNC_NAME \
            --runtime nodejs20.x \
            --handler index.handler \
            --timeout 30 \
            --memory-size 256 \
            --environment "Variables={TIMESTREAM_DB=wildfire-sensor-db,TIMESTREAM_TABLE=sensor-readings,DYNAMODB_TABLE=wildfire-alerts,AWS_REGION=$REGION}" \
            --tracing-config Mode=Active \
            --region $REGION \
            --no-cli-pager > /dev/null

    else
        echo "🆕 Creating new function..."
        aws lambda create-function \
            --function-name $FUNC_NAME \
            --runtime nodejs20.x \
            --role $LAMBDA_ROLE_ARN \
            --handler index.handler \
            --zip-file fileb:///tmp/${FUNC_NAME}.zip \
            --timeout 30 \
            --memory-size 256 \
            --environment "Variables={TIMESTREAM_DB=wildfire-sensor-db,TIMESTREAM_TABLE=sensor-readings,DYNAMODB_TABLE=wildfire-alerts,AWS_REGION=$REGION}" \
            --tracing-config Mode=Active \
            --region $REGION \
            --no-cli-pager > /dev/null
    fi

    # Wait for function to be active
    echo "⏳ Waiting for function to be active..."
    aws lambda wait function-active --function-name $FUNC_NAME --region $REGION

    # Clean up
    rm /tmp/${FUNC_NAME}.zip

    echo "✅ $FUNC_NAME deployed successfully"
    echo ""
done

echo "=================================================="
echo "✅ All Lambda functions deployed!"
echo "=================================================="
echo ""
echo "Deployed functions:"
aws lambda list-functions \
    --region $REGION \
    --query 'Functions[?starts_with(FunctionName, `api-`) || contains(FunctionName, `enricher`) || contains(FunctionName, `processor`)].{Name:FunctionName,Runtime:Runtime,Memory:MemorySize,Updated:LastModified}' \
    --output table

echo ""
echo "Next steps:"
echo "  1. Deploy API Gateway: ./deploy-api.sh"
echo "  2. Deploy EventBridge: ./deploy-eventbridge.sh"
